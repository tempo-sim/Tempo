// Copyright Tempo Simulation, LLC. All Rights Reserved

//! The client half of the shared-memory transport for streams with large `bytes` fields.
//!
//! A generated stream wrapper whose response type has a top-level `bytes` field asks the server for
//! shared memory. A server on this machine that can offer it names the stream's first segment in the
//! call's initial metadata. Once this client has mapped that segment, the server moves each large
//! `bytes` field into a ring in shared memory and sends a fixed-size reference in its place. The
//! stream's decoder puts the payload back before the caller sees the message, so a caller gets the
//! same messages either way. The layout is documented in TempoCore's `TempoSharedMemory.h`.
//!
//! `TEMPO_SHARED_MEMORY=0` turns it off. If the announced segment cannot be mapped (a client in a
//! container without the sim's `/dev/shm`, say), the server goes on sending every payload inline,
//! and no later stream in this process asks again.

use std::marker::PhantomData;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use tonic::codec::{Codec, DecodeBuf, Decoder, ProstCodec};
use tonic::Status;

/// A message whose top-level `bytes` fields may arrive through shared memory. The generated wrappers
/// implement it for the response type of every stream that asks for shared memory.
pub trait SharedMemoryMessage {
    /// Every top-level singular `bytes` field that is not part of a `oneof`.
    fn shared_memory_fields(&mut self) -> Vec<&mut Vec<u8>>;
}

static UNAVAILABLE: AtomicBool = AtomicBool::new(false);

const REF_SIZE: usize = 80;
const REF_MAGIC: &[u8; 8] = b"TEMPOREF";
const SEGMENT_MAGIC: &[u8; 8] = b"TEMPOSHM";
const VERSION: u32 = 1;
const HEADER_SIZE: usize = 64;
const SLOT_ENTRY_SIZE: usize = 16;
const ATTACHED_OFFSET: usize = 24;

/// Whether new streams ask for shared memory.
pub fn enabled() -> bool {
    cfg!(unix)
        && std::env::var("TEMPO_SHARED_MEMORY").map(|v| v != "0").unwrap_or(true)
        && !UNAVAILABLE.load(Ordering::Relaxed)
}

/// The request metadata that asks for shared memory, if this process may.
pub fn request<T>(message: T) -> tonic::Request<T> {
    let mut request = tonic::Request::new(message);
    if enabled() {
        request
            .metadata_mut()
            .insert("tempo-shm", tonic::metadata::MetadataValue::from_static("1"));
    }
    request
}

fn u32_at(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}

fn u64_at(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap())
}

#[cfg(unix)]
mod segment {
    use super::*;
    use std::ffi::CString;
    use std::sync::atomic::AtomicU32;

    /// A client's mapping of one segment.
    pub struct Segment {
        base: *mut u8,
        size: usize,
    }

    // The mapping is only touched through &mut Reader, under its mutex.
    unsafe impl Send for Segment {}

    impl Segment {
        pub fn open(name: &str, size: usize) -> Result<Segment, String> {
            let c_name = CString::new(name).map_err(|e| e.to_string())?;
            // SAFETY: plain libc calls on a valid C string; the mapping is checked before use.
            unsafe {
                let fd = libc::shm_open(c_name.as_ptr(), libc::O_RDWR, 0);
                if fd < 0 {
                    return Err(format!("could not open {name}: {}", std::io::Error::last_os_error()));
                }
                let mapped = libc::mmap(
                    std::ptr::null_mut(),
                    size,
                    libc::PROT_READ | libc::PROT_WRITE,
                    libc::MAP_SHARED,
                    fd,
                    0,
                );
                libc::close(fd);
                if mapped == libc::MAP_FAILED {
                    return Err(format!("could not map {name}: {}", std::io::Error::last_os_error()));
                }
                let segment = Segment { base: mapped as *mut u8, size };
                let header = segment.bytes(0, HEADER_SIZE);
                if &header[0..8] != SEGMENT_MAGIC || u32_at(header, 8) != VERSION {
                    return Err(format!("{name} has a layout this client does not know"));
                }
                segment.atomic(ATTACHED_OFFSET).store(1, Ordering::Release);
                Ok(segment)
            }
        }

        pub fn bytes(&self, offset: usize, length: usize) -> &[u8] {
            assert!(offset.checked_add(length).is_some_and(|end| end <= self.size), "reference out of bounds");
            // SAFETY: in bounds of a live mapping.
            unsafe { std::slice::from_raw_parts(self.base.add(offset), length) }
        }

        pub fn atomic(&self, offset: usize) -> &AtomicU32 {
            assert!(offset + 4 <= self.size && offset % 4 == 0);
            // SAFETY: an aligned u32 in a live, shared mapping, which only atomics touch.
            unsafe { AtomicU32::from_ptr(self.base.add(offset) as *mut u32) }
        }
    }

    impl Drop for Segment {
        fn drop(&mut self) {
            // SAFETY: the mapping made in open.
            unsafe {
                libc::munmap(self.base as *mut libc::c_void, self.size);
            }
        }
    }
}

/// Puts one stream's shared-memory payloads back into its messages.
#[derive(Default)]
pub struct Reader {
    #[cfg(unix)]
    segments: Vec<(String, segment::Segment)>,
}

impl Reader {
    /// Maps the segment the server announced in the call's initial metadata, if it announced one.
    /// Returns whether this client can now receive payloads through shared memory.
    pub fn attach(&mut self, metadata: &tonic::metadata::MetadataMap) -> bool {
        let Some(name) = metadata.get("tempo-shm-segment").and_then(|v| v.to_str().ok()) else {
            return false;
        };
        let size = metadata
            .get("tempo-shm-size")
            .and_then(|v| v.to_str().ok())
            .and_then(|v| v.parse::<usize>().ok())
            .unwrap_or(0);
        match self.segment(name, size) {
            Ok(_) => true,
            Err(e) => {
                UNAVAILABLE.store(true, Ordering::Relaxed);
                eprintln!("Tempo shared memory is unavailable ({}); payloads arrive inline.", e.message());
                false
            }
        }
    }

    #[cfg(unix)]
    fn segment(&mut self, name: &str, size: usize) -> Result<&segment::Segment, Status> {
        if let Some(index) = self.segments.iter().position(|(n, _)| n == name) {
            return Ok(&self.segments[index].1);
        }
        let segment = segment::Segment::open(name, size).map_err(Status::unavailable)?;
        // A stream moves to a new segment only after its last message in the old one.
        self.segments.clear();
        self.segments.push((name.to_string(), segment));
        Ok(&self.segments[0].1)
    }

    #[cfg(not(unix))]
    fn segment(&mut self, _name: &str, _size: usize) -> Result<(), Status> {
        Err(Status::unavailable("no shared memory on this platform"))
    }

    /// Replaces each reference in message with the payload it names, and frees its slot.
    pub fn restore<T: SharedMemoryMessage>(&mut self, message: &mut T) -> Result<(), Status> {
        #[cfg(unix)]
        {
            let mut released = None;
            for field in message.shared_memory_fields() {
                if field.len() != REF_SIZE || &field[0..8] != REF_MAGIC {
                    continue;
                }
                let version = u32_at(field, 8);
                if version != VERSION {
                    return Err(Status::internal(format!(
                        "a shared-memory reference has version {version}; this client knows {VERSION}"
                    )));
                }
                let slot = u32_at(field, 12) as usize;
                let sequence = u64_at(field, 16);
                let offset = u64_at(field, 24) as usize;
                let length = u64_at(field, 32) as usize;
                let size = u64_at(field, 40) as usize;
                let name_bytes = &field[48..80];
                let name_end = name_bytes.iter().position(|&b| b == 0).unwrap_or(name_bytes.len());
                let name = std::str::from_utf8(&name_bytes[..name_end])
                    .map_err(|e| Status::internal(e.to_string()))?
                    .to_string();
                let segment = self.segment(&name, size)?;
                let entry = HEADER_SIZE + slot * SLOT_ENTRY_SIZE;
                let state = segment.atomic(entry).load(Ordering::Acquire);
                let slot_sequence = u64_at(segment.bytes(entry, SLOT_ENTRY_SIZE), 8);
                if state != 1 || slot_sequence != sequence {
                    return Err(Status::internal(format!(
                        "shared-memory slot {slot} holds message {slot_sequence} (state {state}), not {sequence}"
                    )));
                }
                *field = segment.bytes(offset, length).to_vec();
                released = Some(entry);
            }
            if let (Some(entry), Some((_, segment))) = (released, self.segments.first()) {
                segment.atomic(entry).store(0, Ordering::Release);
            }
        }
        #[cfg(not(unix))]
        let _ = message;
        Ok(())
    }
}

/// The codec of a stream that may receive its payloads through shared memory: prost's, with a
/// decoder that restores them.
pub struct SharedMemoryCodec<T, U> {
    reader: Arc<Mutex<Reader>>,
    inner: ProstCodec<T, U>,
}

impl<T, U> SharedMemoryCodec<T, U> {
    pub fn new(reader: Arc<Mutex<Reader>>) -> Self {
        Self { reader, inner: ProstCodec::default() }
    }
}

impl<T, U> Codec for SharedMemoryCodec<T, U>
where
    T: prost::Message + Send + 'static,
    U: prost::Message + Default + SharedMemoryMessage + Send + 'static,
{
    type Encode = T;
    type Decode = U;
    type Encoder = <ProstCodec<T, U> as Codec>::Encoder;
    type Decoder = SharedMemoryDecoder<U>;

    fn encoder(&mut self) -> Self::Encoder {
        self.inner.encoder()
    }

    fn decoder(&mut self) -> Self::Decoder {
        SharedMemoryDecoder { reader: self.reader.clone(), _message: PhantomData }
    }
}

pub struct SharedMemoryDecoder<U> {
    reader: Arc<Mutex<Reader>>,
    _message: PhantomData<fn() -> U>,
}

impl<U> Decoder for SharedMemoryDecoder<U>
where
    U: prost::Message + Default + SharedMemoryMessage,
{
    type Item = U;
    type Error = Status;

    fn decode(&mut self, buf: &mut DecodeBuf<'_>) -> Result<Option<U>, Status> {
        let mut message = U::decode(buf).map_err(|e| Status::internal(e.to_string()))?;
        self.reader.lock().unwrap().restore(&mut message)?;
        Ok(Some(message))
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;

    struct Image {
        data: Vec<u8>,
    }

    impl SharedMemoryMessage for Image {
        fn shared_memory_fields(&mut self) -> Vec<&mut Vec<u8>> {
            vec![&mut self.data]
        }
    }

    // A segment laid out as the server lays one out, from the documented layout.
    struct FakeServerSegment {
        name: String,
        base: *mut u8,
        size: usize,
    }

    impl FakeServerSegment {
        fn new() -> Self {
            let name = format!("/tempo.rs.{:x}", std::process::id());
            let size = 4096 + 2 * 65536;
            let c_name = std::ffi::CString::new(name.clone()).unwrap();
            unsafe {
                libc::shm_unlink(c_name.as_ptr());
                let fd = libc::shm_open(c_name.as_ptr(), libc::O_CREAT | libc::O_EXCL | libc::O_RDWR, 0o600);
                assert!(fd >= 0);
                assert_eq!(libc::ftruncate(fd, size as libc::off_t), 0);
                let base = libc::mmap(std::ptr::null_mut(), size, libc::PROT_READ | libc::PROT_WRITE, libc::MAP_SHARED, fd, 0) as *mut u8;
                libc::close(fd);
                let header = std::slice::from_raw_parts_mut(base, 64);
                header[0..8].copy_from_slice(SEGMENT_MAGIC);
                header[8..12].copy_from_slice(&VERSION.to_le_bytes());
                header[12..16].copy_from_slice(&2u32.to_le_bytes());
                header[16..24].copy_from_slice(&65536u64.to_le_bytes());
                header[32..40].copy_from_slice(&4096u64.to_le_bytes());
                FakeServerSegment { name, base, size }
            }
        }

        fn write(&self, slot: usize, sequence: u64, payload: &[u8]) -> Vec<u8> {
            let offset = 4096 + slot * 65536;
            unsafe {
                std::slice::from_raw_parts_mut(self.base.add(offset), payload.len()).copy_from_slice(payload);
                let entry = std::slice::from_raw_parts_mut(self.base.add(64 + slot * 16), 16);
                entry[8..16].copy_from_slice(&sequence.to_le_bytes());
                entry[0..4].copy_from_slice(&1u32.to_le_bytes());
            }
            let mut reference = Vec::with_capacity(REF_SIZE);
            reference.extend_from_slice(REF_MAGIC);
            reference.extend_from_slice(&VERSION.to_le_bytes());
            reference.extend_from_slice(&(slot as u32).to_le_bytes());
            reference.extend_from_slice(&sequence.to_le_bytes());
            reference.extend_from_slice(&(offset as u64).to_le_bytes());
            reference.extend_from_slice(&(payload.len() as u64).to_le_bytes());
            reference.extend_from_slice(&(self.size as u64).to_le_bytes());
            let mut name = self.name.as_bytes().to_vec();
            name.resize(32, 0);
            reference.extend_from_slice(&name);
            reference
        }

        fn u32_at(&self, offset: usize) -> u32 {
            unsafe { u32_at(std::slice::from_raw_parts(self.base, self.size), offset) }
        }
    }

    impl Drop for FakeServerSegment {
        fn drop(&mut self) {
            let c_name = std::ffi::CString::new(self.name.clone()).unwrap();
            unsafe {
                libc::munmap(self.base as *mut libc::c_void, self.size);
                libc::shm_unlink(c_name.as_ptr());
            }
        }
    }

    #[test]
    fn a_reference_is_replaced_by_its_payload_and_frees_its_slot() {
        let server = FakeServerSegment::new();
        let payload: Vec<u8> = (0..50_000u32).map(|i| (i % 251) as u8).collect();
        let mut image = Image { data: server.write(1, 9, &payload) };

        let mut metadata = tonic::metadata::MetadataMap::new();
        metadata.insert("tempo-shm-segment", server.name.parse().unwrap());
        metadata.insert("tempo-shm-size", server.size.to_string().parse().unwrap());
        let mut reader = Reader::default();
        assert!(reader.attach(&metadata));
        assert_eq!(server.u32_at(ATTACHED_OFFSET), 1);

        reader.restore(&mut image).unwrap();
        assert_eq!(image.data, payload);
        assert_eq!(server.u32_at(64 + 16), 0, "the slot was not freed");
    }

    #[test]
    fn an_inline_payload_is_left_alone() {
        let mut image = Image { data: vec![1u8; REF_SIZE] };
        Reader::default().restore(&mut image).unwrap();
        assert_eq!(image.data, vec![1u8; REF_SIZE]);
    }
}
