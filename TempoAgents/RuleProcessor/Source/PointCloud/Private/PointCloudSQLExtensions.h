// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct sqlite3;
struct sqlite3_context;
struct sqlite3_value;
struct sqlite3_rtree_query_info;
struct SHA3Context;

#include "IncludeSQLite.h"
#include "Math/Box.h"

class SQLExtension
{
public:
	// Helper function that is called whenever a new object is added to the DB
	static void objectadded(sqlite3_context* context, int argc, sqlite3_value** argv);

	// Helper function that is called whenever an object is removed from the DB
	static void objectremoved(sqlite3_context* context, int argc, sqlite3_value** argv);

	// Helper function that is called whenever a new object is added to the DB
	static void sqlsqrt(sqlite3_context* context, int argc, sqlite3_value** argv);

	// Helper function that is called whenever a new object is added to the DB
	static void sqlpow(sqlite3_context* context, int argc, sqlite3_value** argv);

	// Helper function that is called whenever a new object is added to the DB
	static void sqlIsInSphere(sqlite3_context* context, int argc, sqlite3_value** argv);

	/**
	* Helper function to filter points inside an oriented bounding box.
	* IN_OBB(
	*	Rotation.Pitch, Rotation.Yaw, Rotation.Roll,
	*	Translation.X, Translation.Y, Translation.Z,
	*	Scale.X, Scale.Y, Scale.Z,
	*	Point.X, Point.Y, Point.Z)
	* 
	* Rotation - box orientation.
	* Translation - box center.
	* Scale - box size.
	*/
	static void sqlIsInOBB(sqlite3_context* context, int argc, sqlite3_value** argv);

	// Helper function to query the bounds of a table/view
	static FBox query_rtree_bbox(sqlite3* db_handle, const char* rtree_name);

	/**
	* Implementation of the sha3(X,SIZE) function.
	*
	* Return a BLOB which is the SIZE-bit SHA3 hash of X.  The default
	* size is 256.  If X is a BLOB, it is hashed as is.
	* For all other non-NULL types of input, X is converted into a UTF-8 string
	* and the string is hashed without the trailing 0x00 terminator.  The hash
	* of a NULL value is NULL.
	*/
	static void sha3Func(sqlite3_context* context, int argc, sqlite3_value** argv);

	/**
	* Implementation of the sha3_query(SQL,SIZE,INCLUDESQL) function.
	*
	* This function compiles and runs the SQL statement(s) given in the
	* argument. The results are hashed using a SIZE-bit SHA3.  The default
	* size is 256.
	* INCLUDESQL is an optional flag to determine inclusion of the sql statement in the final hash.
	*
	* The format of the byte stream that is hashed is summarized as follows:
	*
	*       S<n>:<sql>
	*       R
	*       N
	*       I<int>
	*       F<ieee-float>
	*       B<size>:<bytes>
	*       T<size>:<text>
	*
	* <sql> is the original SQL text for each statement run and <n> is
	* the size of that text.  The SQL text is UTF-8.  A single R character
	* occurs before the start of each row.  N means a NULL value.
	* I mean an 8-byte little-endian integer <int>.  F is a floating point
	* number with an 8-byte little-endian IEEE floating point value <ieee-float>.
	* B means blobs of <size> bytes.  T means text rendered as <size>
	* bytes of UTF-8.  The <n> and <size> values are expressed as an ASCII
	* text integers.
	*
	* For each SQL statement in the X input, there is one S segment.  Each
	* S segment is followed by zero or more R segments, one for each row in the
	* result set.  After each R, there are one or more N, I, F, B, or T segments,
	* one for each column in the result set.  Segments are concatenated directly
	* with no delimiters of any kind.
	*/
	static void sha3QueryFunc(sqlite3_context* context, int argc, sqlite3_value** argv);

	static int Sha3CallBack(void* UsrData, int argc, char** argv, char** azColName);

private:
	
	// A single step of the Keccak mixing function for a 1600-bit state
	static void KeccakF1600Step(SHA3Context* p);

	/**
	* Initialize a new hash.  iSize determines the size of the hash
	* in bits and should be one of 224, 256, 384, or 512.  Or iSize
	* can be zero to use the default hash size of 256 bits.
	*/
	static void SHA3Init(SHA3Context* p, int iSize);

	/**
	* Make consecutive calls to the SHA3Update function to add new content
	* to the hash
	*/
	static void SHA3Update(SHA3Context* p, const unsigned char* aData, unsigned int nData);

	/**
	* After all content has been added, invoke SHA3Final() to compute
	* the final hash.  The function returns a pointer to the binary
	* hash value.
	*/
	static unsigned char* SHA3Final(SHA3Context* p);

	/**
	* Compute a string using sqlite3_vsnprintf() with a maximum length
	* of 50 bytes and add it to the hash.
	*/
	static void hash_step_vformat(SHA3Context* p, const char* zFormat, ...);
};
