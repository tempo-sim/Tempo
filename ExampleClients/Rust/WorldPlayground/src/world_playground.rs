// Copyright Tempo Simulation, LLC. All Rights Reserved

//! World Playground - interactive TempoWorld client (Rust port of WorldPlayground.py).
//!
//! Spawn/destroy actors and components, set properties (including drilling
//! into nested struct/array values).

use std::collections::HashMap;

use clap::Parser;
use inquire::{Select, Text};
use tempo_sim::proto::tempo_core::{Rotation, Transform, Vector};
use tempo_sim::proto::tempo_world::value::Value as V;
use tempo_sim::proto::tempo_world::{
    BoolArray, Color, FloatArray, Int64Array, IntArray, StringArray, Value,
};
use tempo_sim::{set_server_async, tempo_world};

#[derive(Parser, Debug)]
struct Args {
    #[arg(long, default_value = "0.0.0.0", help = "IP address of machine where Tempo is running")]
    ip: String,
    #[arg(long, default_value_t = 10001u16, help = "Port Tempo gRPC server is using")]
    port: u16,
}

// ---------------------------------------------------------------------------
// Prompt helpers
// ---------------------------------------------------------------------------

async fn select_one(prompt: &str, options: Vec<String>) -> Option<String> {
    if options.is_empty() {
        return None;
    }
    let prompt = prompt.to_string();
    tokio::task::spawn_blocking(move || Select::new(&prompt, options).prompt().ok())
        .await
        .ok()
        .flatten()
}

async fn text_input(prompt: &str, default: &str) -> Option<String> {
    let prompt = prompt.to_string();
    let default = default.to_string();
    tokio::task::spawn_blocking(move || {
        if default.is_empty() {
            Text::new(&prompt).prompt().ok()
        } else {
            Text::new(&prompt).with_default(&default).prompt().ok()
        }
    })
    .await
    .ok()
    .flatten()
}

// ---------------------------------------------------------------------------
// Transform helpers
// ---------------------------------------------------------------------------

fn parse_transform(text: &str) -> Result<Transform, String> {
    let parts: Vec<f64> = text
        .split_whitespace()
        .map(|s| s.parse::<f64>())
        .collect::<Result<_, _>>()
        .map_err(|e| format!("Failed to parse number: {}", e))?;
    if parts.len() != 6 {
        return Err("Expected 6 values: X Y Z Roll Pitch Yaw".into());
    }
    Ok(Transform {
        location: Some(Vector {
            x: parts[0],
            y: parts[1],
            z: parts[2],
        }),
        rotation: Some(Rotation {
            r: parts[3] * std::f64::consts::PI / 180.0,
            p: parts[4] * std::f64::consts::PI / 180.0,
            y: parts[5] * std::f64::consts::PI / 180.0,
        }),
        ..Default::default()
    })
}

fn format_transform(t: &Transform) -> String {
    let l = t.location.clone().unwrap_or(Vector { x: 0.0, y: 0.0, z: 0.0 });
    let r = t.rotation.clone().unwrap_or(Rotation { r: 0.0, p: 0.0, y: 0.0 });
    let to_deg = 180.0 / std::f64::consts::PI;
    format!(
        "Location({:.2}, {:.2}, {:.2}) Rotation({:.1}, {:.1}, {:.1})",
        l.x,
        l.y,
        l.z,
        r.r * to_deg,
        r.p * to_deg,
        r.y * to_deg,
    )
}

// ---------------------------------------------------------------------------
// Value parsing
// ---------------------------------------------------------------------------

fn parse_bool(s: &str) -> Result<bool, String> {
    match s.trim().to_lowercase().as_str() {
        "true" | "1" | "yes" => Ok(true),
        "false" | "0" | "no" => Ok(false),
        _ => Err(format!(
            "Cannot parse '{}' as bool. Use true/false, 1/0, or yes/no.",
            s
        )),
    }
}

fn split_top_level(s: &str, delimiter: char) -> Vec<String> {
    let mut parts = Vec::new();
    let mut depth: i32 = 0;
    let mut current = String::new();
    for ch in s.chars() {
        match ch {
            '(' | '{' => {
                depth += 1;
                current.push(ch);
            }
            ')' | '}' => {
                depth -= 1;
                current.push(ch);
            }
            c if c == delimiter && depth == 0 => {
                let trimmed = current.trim().to_string();
                parts.push(trimmed);
                current.clear();
            }
            c => current.push(c),
        }
    }
    let tail = current.trim().to_string();
    if !tail.is_empty() {
        parts.push(tail);
    }
    parts
}

fn unwrap_value(v: &str) -> Option<(String, char)> {
    let v = v.trim();
    if v.starts_with('(') && v.ends_with(')') && v.len() >= 2 {
        Some((v[1..v.len() - 1].to_string(), '='))
    } else if v.starts_with('{') && v.ends_with('}') && v.len() >= 2 {
        Some((v[1..v.len() - 1].to_string(), ':'))
    } else {
        None
    }
}

fn is_composite_value(v: &str) -> bool {
    let v = v.trim();
    (v.starts_with('(') && v.ends_with(')'))
        || (v.starts_with('{') && v.ends_with('}'))
}

fn infer_type(value: &str) -> (String, bool) {
    let v = value.trim();
    let lc = v.to_lowercase();
    if lc == "true" || lc == "false" {
        return ("bool".into(), true);
    }
    if is_composite_value(v) {
        return ("struct".into(), false);
    }
    if v.parse::<i64>().is_ok() {
        return ("int".into(), true);
    }
    if v.parse::<f64>().is_ok() {
        return ("float".into(), true);
    }
    ("string".into(), true)
}

#[derive(Clone, Debug)]
struct StructMember {
    name: String,
    value: String,
    inferred: String,
}

fn parse_struct_value(value: &str) -> Vec<StructMember> {
    let Some((inner, sep)) = unwrap_value(value) else {
        return Vec::new();
    };
    let mut members = Vec::new();
    for part in split_top_level(&inner, ',') {
        let Some(sep_pos) = part.find(sep) else { continue };
        let name = part[..sep_pos].trim().to_string();
        if name.is_empty() {
            continue;
        }
        let val = part[sep_pos + 1..].trim().to_string();
        let (inferred, _) = infer_type(&val);
        members.push(StructMember {
            name,
            value: val,
            inferred,
        });
    }
    members
}

#[derive(Clone, Debug)]
struct ArrayElement {
    value: String,
    inferred: String,
}

fn parse_array_value(value: &str) -> Vec<ArrayElement> {
    let Some((inner, sep)) = unwrap_value(value) else {
        return Vec::new();
    };
    let mut elements = Vec::new();
    for part in split_top_level(&inner, ',') {
        let val = part.trim().to_string();
        if val.contains(sep) && !is_composite_value(&val) {
            return Vec::new();
        }
        let (inferred, _) = infer_type(&val);
        elements.push(ArrayElement {
            value: val,
            inferred,
        });
    }
    elements
}

const LEAF_TYPES: &[&str] = &[
    "bool", "string", "enum", "int", "int32", "int64", "float", "double", "vector", "rotator",
    "color", "class", "asset", "actor", "component",
];

fn is_leaf_type(prop_type: &str) -> bool {
    let t = prop_type.trim().to_lowercase();
    if LEAF_TYPES.iter().any(|&s| s == t) {
        return true;
    }
    if let Some(stripped) = t.strip_suffix("[]") {
        return LEAF_TYPES.iter().any(|&s| s == stripped);
    }
    false
}

fn type_hint(prop_type: &str) -> &'static str {
    match prop_type.trim().to_lowercase().as_str() {
        "bool" => " (true/false)",
        "vector" => " (X Y Z, centimeters, left-handed)",
        "rotator" => " (Roll Pitch Yaw, degrees)",
        "color" => " (R G B, 0-255)",
        t if t.ends_with("[]") => " (comma-separated values)",
        _ => "",
    }
}

// ---------------------------------------------------------------------------
// Property setter dispatch
// ---------------------------------------------------------------------------

fn parse_floats<const N: usize>(text: &str, what: &str) -> Result<[f64; N], String> {
    let parts: Vec<&str> = text.split_whitespace().collect();
    if parts.len() != N {
        return Err(format!("{} requires {} values", what, N));
    }
    let mut values = [0.0; N];
    for (value, part) in values.iter_mut().zip(parts) {
        *value = part.parse().map_err(|e: std::num::ParseFloatError| e.to_string())?;
    }
    Ok(values)
}

/// The Value a property of this type is written from. A property is written in Unreal's own units
/// -- centimeters, degrees, left-handed -- exactly as it stores them.
fn leaf_value(t: &str, v: String) -> Result<V, String> {
    Ok(match t {
        "bool" => V::BoolValue(parse_bool(&v)?),
        "string" => V::StringValue(v),
        "enum" => V::EnumValue(v),
        "int" | "int32" => {
            V::IntValue(v.parse().map_err(|e: std::num::ParseIntError| e.to_string())?)
        }
        "int64" => V::Int64Value(v.parse().map_err(|e: std::num::ParseIntError| e.to_string())?),
        "float" | "double" => {
            V::FloatValue(v.parse().map_err(|e: std::num::ParseFloatError| e.to_string())?)
        }
        "vector" => {
            let [x, y, z] = parse_floats(&v, "Vector (X Y Z)")?;
            V::VectorValue(Vector { x, y, z })
        }
        "rotator" => {
            let [r, p, y] = parse_floats(&v, "Rotator (Roll Pitch Yaw)")?;
            V::RotatorValue(Rotation { r, p, y })
        }
        "color" => {
            let parts: Vec<&str> = v.split_whitespace().collect();
            if parts.len() != 3 {
                return Err("Color requires 3 values: R G B (0-255)".into());
            }
            let channel = |s: &str| s.parse::<i32>().map_err(|e| e.to_string());
            V::ColorValue(Color {
                r: channel(parts[0])?,
                g: channel(parts[1])?,
                b: channel(parts[2])?,
                a: None,
            })
        }
        "class" => V::ClassValue(v),
        "asset" => V::AssetValue(v),
        "actor" => V::ActorValue(v),
        "component" => V::ComponentValue(v),
        _ => {
            println!("  Unsupported property type: {}. Attempting as string.", t);
            V::StringValue(v)
        }
    })
}

async fn set_property_value(
    actor: &str,
    component: &str,
    prop_name: &str,
    prop_type: &str,
    value_text: &str,
) -> Result<(), String> {
    let t = prop_type.trim().to_lowercase();
    let value = match t.strip_suffix("[]") {
        Some(base) => array_value(base, value_text)?,
        None => leaf_value(&t, value_text.to_string())?,
    };
    tempo_world::set_property_async(tempo_world::SetPropertyRequest {
        actor: actor.to_string(),
        component: component.to_string(),
        property: prop_name.to_string(),
        value: Some(Value { value: Some(value) }),
        ..Default::default()
    })
    .await
    .map(|_| ())
    .map_err(|e| e.to_string())
}

fn array_value(base: &str, value_text: &str) -> Result<V, String> {
    let parts: Vec<String> = value_text.split(',').map(|v| v.trim().to_string()).collect();
    let strings = |values| StringArray { values };
    Ok(match base {
        "bool" => {
            let values: Result<Vec<bool>, String> = parts.iter().map(|v| parse_bool(v)).collect();
            V::BoolArrayValue(BoolArray { values: values? })
        }
        "string" => V::StringArrayValue(strings(parts)),
        "enum" => V::EnumArrayValue(strings(parts)),
        "int" | "int32" => {
            let values: Result<Vec<i32>, _> = parts.iter().map(|v| v.parse::<i32>()).collect();
            V::IntArrayValue(IntArray { values: values.map_err(|e| e.to_string())? })
        }
        "int64" => {
            let values: Result<Vec<i64>, _> = parts.iter().map(|v| v.parse::<i64>()).collect();
            V::Int64ArrayValue(Int64Array { values: values.map_err(|e| e.to_string())? })
        }
        "float" | "double" => {
            let values: Result<Vec<f32>, _> = parts.iter().map(|v| v.parse::<f32>()).collect();
            V::FloatArrayValue(FloatArray { values: values.map_err(|e| e.to_string())? })
        }
        "class" => V::ClassArrayValue(strings(parts)),
        "asset" => V::AssetArrayValue(strings(parts)),
        "actor" => V::ActorArrayValue(strings(parts)),
        "component" => V::ComponentArrayValue(strings(parts)),
        _ => {
            println!("  Unsupported array type: {}[]. Attempting as string array.", base);
            V::StringArrayValue(strings(parts))
        }
    })
}

// ---------------------------------------------------------------------------
// Fetchers
// ---------------------------------------------------------------------------

async fn fetch_actors() -> Vec<(String, String)> {
    match tempo_world::get_all_actors_async().await {
        Ok(r) => r
            .actors
            .into_iter()
            .map(|a| (a.name.clone(), format!("{} ({})", a.name, a.actor_type)))
            .collect(),
        Err(e) => {
            println!("\n  Error fetching actors: {}", e);
            Vec::new()
        }
    }
}

async fn fetch_components(actor: &str) -> Vec<(String, String)> {
    match tempo_world::get_all_components_async(tempo_world::GetAllComponentsRequest {
        actor: actor.to_string(),
    })
    .await {
        Ok(r) => r
            .components
            .into_iter()
            .map(|c| (c.name.clone(), format!("{} ({})", c.name, c.component_type)))
            .collect(),
        Err(e) => {
            println!("\n  Error fetching components: {}", e);
            Vec::new()
        }
    }
}

#[derive(Clone, Debug)]
struct Property {
    name: String,
    property_type: String,
    value: String,
}

async fn fetch_properties(actor: &str, component: Option<&str>) -> Vec<Property> {
    let resp = if let Some(c) = component {
        tempo_world::get_component_properties_async(tempo_world::GetComponentPropertiesRequest {
            actor: actor.to_string(),
            component: c.to_string(),
        })
        .await
    } else {
        tempo_world::get_actor_properties_async(tempo_world::GetActorPropertiesRequest {
            actor: actor.to_string(),
            include_components: false,
        })
        .await
    };
    match resp {
        Ok(r) => r
            .properties
            .into_iter()
            .filter(|p| p.property_type != "unsupported")
            .map(|p| Property {
                name: p.name,
                property_type: p.property_type,
                value: p.value,
            })
            .collect(),
        Err(e) => {
            println!("\n  Error fetching properties: {}", e);
            Vec::new()
        }
    }
}

// Pick from (key, label) list. Returns the key.
async fn pick_keyed(prompt: &str, items: Vec<(String, String)>) -> Option<String> {
    if items.is_empty() {
        return None;
    }
    let mut map: HashMap<String, String> = HashMap::new();
    let mut labels: Vec<String> = Vec::with_capacity(items.len());
    for (k, l) in items {
        labels.push(l.clone());
        map.insert(l, k);
    }
    let chosen = select_one(prompt, labels).await?;
    map.remove(&chosen)
}

// ---------------------------------------------------------------------------
// Flows
// ---------------------------------------------------------------------------

async fn flow_spawn_actor() {
    println!("\n--- Spawn Actor ---");
    let Some(actor_type) = text_input("Actor type (e.g. BP_SensorRig)", "").await else {
        return;
    };
    if actor_type.is_empty() {
        println!("  No type provided, cancelled.");
        return;
    }
    let Some(transform_text) =
        text_input("Transform (X Y Z Roll Pitch Yaw, degrees)", "0 0 0 0 0 0").await
    else {
        return;
    };
    let transform = match parse_transform(&transform_text) {
        Ok(t) => t,
        Err(e) => {
            println!("  {}", e);
            return;
        }
    };
    let request = tempo_world::SpawnActorRequest {
        actor_type,
        transform: Some(transform),
        ..Default::default()
    };
    match tempo_world::spawn_actor_async(request).await {
        Ok(resp) => {
            println!("\n  Spawned: {}", resp.name);
            if let Some(t) = &resp.transform {
                println!("  Transform: {}", format_transform(t));
            }
        }
        Err(e) => println!("  Error: {}", e),
    }
}

async fn flow_destroy_actor() {
    println!("\n--- Destroy Actor ---");
    let actors = fetch_actors().await;
    if actors.is_empty() {
        println!("  No actors found.");
        return;
    }
    let Some(actor) = pick_keyed("Select actor to destroy", actors).await else {
        return;
    };
    match tempo_world::destroy_actor_async(tempo_world::DestroyActorRequest { actor: actor.clone() })
        .await
    {
        Ok(_) => println!("\n  Destroyed: {}", actor),
        Err(e) => println!("  Error: {}", e),
    }
}

async fn flow_add_component() {
    println!("\n--- Add Component ---");
    let Some(component_type) = text_input("Component type (e.g. TempoCamera)", "").await else {
        return;
    };
    if component_type.is_empty() {
        println!("  No type provided, cancelled.");
        return;
    }
    let actors = fetch_actors().await;
    if actors.is_empty() {
        println!("  No actors found.");
        return;
    }
    let Some(actor) = pick_keyed("Select owner actor", actors).await else {
        return;
    };
    let Some(name) = text_input("Component name (optional, leave empty for auto)", "").await
    else {
        return;
    };
    let components = fetch_components(&actor).await;
    let mut parent = String::new();
    if !components.is_empty() {
        let none_label = "(Root Component)";
        let mut items: Vec<(String, String)> = vec![(none_label.to_string(), none_label.to_string())];
        items.extend(components);
        let Some(chosen) = pick_keyed("Select parent component", items).await else {
            return;
        };
        if chosen != none_label {
            parent = chosen;
        }
    }
    let identity = Transform {
        location: Some(Vector { x: 0.0, y: 0.0, z: 0.0 }),
        rotation: Some(Rotation { r: 0.0, p: 0.0, y: 0.0 }),
        scale: Some(Vector { x: 1.0, y: 1.0, z: 1.0 }),
    };
    let request = tempo_world::AddComponentRequest {
        component_type,
        actor,
        name,
        parent,
        transform: Some(identity),
        socket: String::new(),
    };
    match tempo_world::add_component_async(request).await {
        Ok(resp) => {
            println!("\n  Added component: {}", resp.name);
            if let Some(t) = &resp.transform {
                println!("  Transform: {}", format_transform(t));
            }
        }
        Err(e) => println!("  Error: {}", e),
    }
}

async fn flow_destroy_component() {
    println!("\n--- Destroy Component ---");
    let actors = fetch_actors().await;
    if actors.is_empty() {
        println!("  No actors found.");
        return;
    }
    let Some(actor) = pick_keyed("Select actor", actors).await else {
        return;
    };
    let components = fetch_components(&actor).await;
    if components.is_empty() {
        println!("  No components found on {}.", actor);
        return;
    }
    let Some(component) = pick_keyed("Select component to destroy", components).await else {
        return;
    };
    let request = tempo_world::DestroyComponentRequest {
        actor: actor.clone(),
        component: component.clone(),
    };
    match tempo_world::destroy_component_async(request).await {
        Ok(_) => println!("\n  Destroyed: {}:{}", actor, component),
        Err(e) => println!("  Error: {}", e),
    }
}

async fn flow_set_property() {
    println!("\n--- Set Property ---");

    let actors = fetch_actors().await;
    if actors.is_empty() {
        println!("  No actors found.");
        return;
    }
    let Some(actor) = pick_keyed("Select actor", actors).await else {
        return;
    };

    // Component selection (optional)
    let components = fetch_components(&actor).await;
    let mut component: Option<String> = None;
    if !components.is_empty() {
        let none_label = "(Actor properties only)";
        let mut items: Vec<(String, String)> = vec![(none_label.to_string(), none_label.to_string())];
        items.extend(components);
        let Some(chosen) = pick_keyed("Select component", items).await else {
            return;
        };
        if chosen != none_label {
            component = Some(chosen);
        }
    }

    let properties = fetch_properties(&actor, component.as_deref()).await;
    if properties.is_empty() {
        let target = component
            .as_deref()
            .map(|c| format!("{}:{}", actor, c))
            .unwrap_or_else(|| actor.clone());
        println!("  No settable properties found on {}.", target);
        return;
    }

    let prop_items: Vec<(String, String)> = properties
        .iter()
        .map(|p| {
            (
                p.name.clone(),
                format!("{} ({}) = {}", p.name, p.property_type, p.value),
            )
        })
        .collect();
    let Some(prop_name) = pick_keyed("Select property", prop_items).await else {
        return;
    };
    let Some(prop) = properties.iter().find(|p| p.name == prop_name).cloned() else {
        return;
    };

    println!("\n  Property: {}", prop.name);
    println!("  Type:     {}", prop.property_type);
    println!("  Current:  {}", prop.value);

    let original_name = prop.name.clone();
    let original_value = prop.value.clone();
    let mut prop_path = prop.name.clone();
    let mut prop_type = prop.property_type.clone();
    let mut current_value_str = prop.value.clone();

    // Drill into composite types
    while !is_leaf_type(&prop_type) {
        let struct_members = parse_struct_value(&current_value_str);
        let array_elements = if struct_members.is_empty() {
            parse_array_value(&current_value_str)
        } else {
            Vec::new()
        };

        if !struct_members.is_empty() {
            println!(
                "\n  '{}' is a struct ({}) with {} members.",
                prop_path,
                prop_type,
                struct_members.len()
            );
            let items: Vec<(String, String)> = struct_members
                .iter()
                .map(|m| {
                    (
                        m.name.clone(),
                        format!("{} ({}) = {}", m.name, m.inferred, m.value),
                    )
                })
                .collect();
            let Some(member_name) = pick_keyed("Select member", items).await else {
                return;
            };
            let Some(m) = struct_members.iter().find(|m| m.name == member_name) else {
                return;
            };
            prop_path = format!("{}.{}", prop_path, m.name);
            current_value_str = m.value.clone();
            prop_type = m.inferred.clone();
            println!("\n  Path: {}", prop_path);
            println!("  Type: {} (auto-detected)", prop_type);
            println!("  Current: {}", current_value_str);
        } else if !array_elements.is_empty() {
            println!(
                "\n  '{}' is an array ({}) with {} elements.",
                prop_path,
                prop_type,
                array_elements.len()
            );
            let items: Vec<(String, String)> = array_elements
                .iter()
                .enumerate()
                .map(|(i, e)| {
                    (
                        i.to_string(),
                        format!("[{}] ({}) = {}", i, e.inferred, e.value),
                    )
                })
                .collect();
            let Some(idx_str) = pick_keyed("Select index", items).await else {
                return;
            };
            let idx: usize = match idx_str.parse() {
                Ok(i) => i,
                Err(_) => return,
            };
            let e = &array_elements[idx];
            prop_path = format!("{}.{}", prop_path, idx);
            current_value_str = e.value.clone();
            prop_type = e.inferred.clone();
            println!("\n  Path: {}", prop_path);
            println!("  Type: {} (auto-detected)", prop_type);
            println!("  Current: {}", current_value_str);
        } else {
            // Manual entry
            println!(
                "\n  '{}' is a composite type ({}).",
                prop_path, prop_type
            );
            println!("  Enter a member name (for structs) or index (for arrays),");
            println!("  or leave empty to set the whole property as a string.\n");
            let Some(member) = text_input("Member name or index", "").await else {
                return;
            };
            if member.is_empty() {
                prop_type = "string".to_string();
                break;
            }
            prop_path = format!("{}.{}", prop_path, member);
            println!("\n  Path is now: {}", prop_path);

            let none_label = "(Another struct/array - keep drilling)";
            let type_choices: Vec<(String, String)> = vec![
                ("bool".into(), "bool (true/false)".into()),
                ("int".into(), "int".into()),
                ("float".into(), "float".into()),
                ("string".into(), "string".into()),
                ("enum".into(), "enum".into()),
                ("vector".into(), "vector (X Y Z)".into()),
                ("rotator".into(), "rotator (Roll Pitch Yaw)".into()),
                ("color".into(), "color (R G B, 0-255)".into()),
                ("class".into(), "class".into()),
                ("asset".into(), "asset".into()),
                ("actor".into(), "actor reference".into()),
                ("component".into(), "component reference".into()),
            ];
            let mut items: Vec<(String, String)> = vec![(none_label.to_string(), none_label.to_string())];
            items.extend(type_choices);
            let Some(chosen) = pick_keyed(&format!("What type is '{}'?", prop_path), items).await
            else {
                return;
            };
            if chosen == none_label {
                prop_type = "struct".to_string();
                current_value_str.clear();
                continue;
            }
            prop_type = chosen;
        }
    }

    let hint = type_hint(&prop_type);
    let default_val = if prop_path == original_name {
        original_value
    } else {
        current_value_str.clone()
    };
    let Some(new_value) = text_input(&format!("New value{}", hint), &default_val).await else {
        return;
    };

    let comp_str = component.as_deref().unwrap_or("").to_string();
    match set_property_value(&actor, &comp_str, &prop_path, &prop_type, &new_value).await {
        Ok(_) => println!("\n  Successfully set {} = {}", prop_path, new_value),
        Err(e) => println!("  Error: {}", e),
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

#[tokio::main]
async fn main() {
    let args = Args::parse();
    if args.ip != "0.0.0.0" || args.port != 10001 {
        set_server_async(&args.ip, args.port).await;
    }

    println!("\n=== World Playground ===");
    println!("Control actors, components, and properties at runtime.\n");

    let actions = vec![
        "Spawn Actor",
        "Destroy Actor",
        "Add Component",
        "Destroy Component",
        "Set Property",
        "Quit",
    ];

    loop {
        let options: Vec<String> = actions.iter().map(|s| s.to_string()).collect();
        let Some(action) = select_one("What would you like to do?", options).await else {
            break;
        };
        match action.as_str() {
            "Spawn Actor" => flow_spawn_actor().await,
            "Destroy Actor" => flow_destroy_actor().await,
            "Add Component" => flow_add_component().await,
            "Destroy Component" => flow_destroy_component().await,
            "Set Property" => flow_set_property().await,
            "Quit" => break,
            _ => {}
        }
    }

    println!("\nBye!\n");
}
