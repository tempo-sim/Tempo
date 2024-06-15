// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSQLExtensions.h"
#include "IncludeSQLite.h"
#include "PointCloud.h"
#include <limits>

void SQLExtension::objectadded(sqlite3_context* context, int argc, sqlite3_value** argv)
{
	/*for(int i=0;i<argc; i++)
	{
		double value = sqlite3_value_double(argv[i]);
		printf("%.2f ", (float)value);

	}
	printf("\n");*/
	sqlite3_result_null(context);
}

void SQLExtension::objectremoved(sqlite3_context* context, int argc, sqlite3_value** argv)
{
	/*printf("Delete ");
	for(int i=0;i<argc; i++)
	{
		double value = sqlite3_value_double(argv[i]);
		printf("%.2f ", (float)value);

	}
	printf("\n");*/
	sqlite3_result_null(context);
}

void SQLExtension::sqlsqrt(sqlite3_context* context, int argc, sqlite3_value** argv)
{
	if (argc != 1)
	{
		return;
	}

	double value = FMath::Sqrt((float)sqlite3_value_double(argv[0]));

	sqlite3_result_double(context, value);
}

void SQLExtension::sqlpow(sqlite3_context* context, int argc, sqlite3_value** argv)
{
	if (argc != 2)
	{
		return;
	}

	double value = FMath::Pow((float)sqlite3_value_double(argv[0]), (float)sqlite3_value_double(argv[1]));

	sqlite3_result_double(context, value);
}

void SQLExtension::sqlIsInSphere(sqlite3_context* context, int argc, sqlite3_value** argv)
{
	if (argc != 7)
	{
		return;
	}

	double sx = (float)sqlite3_value_double(argv[0]);
	double sy = (float)sqlite3_value_double(argv[1]);
	double sz = (float)sqlite3_value_double(argv[2]);
	double sr = (float)sqlite3_value_double(argv[3]);

	double px = (float)sqlite3_value_double(argv[4]);
	double py = (float)sqlite3_value_double(argv[5]);
	double pz = (float)sqlite3_value_double(argv[6]);

	float dx = px - sx;
	float dy = py - sy;
	float dz = pz - sz;

	if (FMath::Pow(dx, 2) + FMath::Pow(dy, 2) + FMath::Pow(dz, 2) < FMath::Pow(sr, 2))
	{
		sqlite3_result_double(context, 1);
	}
	else
	{
		sqlite3_result_double(context, 0);
	}
}

void SQLExtension::sqlIsInOBB(sqlite3_context* context, int argc, sqlite3_value** argv)
{
	if (argc != 12)
	{
		return;
	}

	const FRotator Rotation((float)sqlite3_value_double(argv[0]), (float)sqlite3_value_double(argv[1]), (float)sqlite3_value_double(argv[2]));
	const FVector Translation((float)sqlite3_value_double(argv[3]), (float)sqlite3_value_double(argv[4]), (float)sqlite3_value_double(argv[5]));
	const FVector Scale((float)sqlite3_value_double(argv[6]), (float)sqlite3_value_double(argv[7]), (float)sqlite3_value_double(argv[8]));
	const FVector Point((float)sqlite3_value_double(argv[9]), (float)sqlite3_value_double(argv[10]), (float)sqlite3_value_double(argv[11]));

	const FTransform Transform(Rotation, Translation, Scale);

	// TODO, pre-invert transfom instead of doing it for every point.
	const FVector LocalPoint = Transform.InverseTransformPosition(Point);
	if (FMath::Abs(LocalPoint.X) <= 1.0f &&
		FMath::Abs(LocalPoint.Y) <= 1.0f &&
		FMath::Abs(LocalPoint.Z) <= 1.0f)
	{
		sqlite3_result_double(context, 1);
	}
	else
	{
		sqlite3_result_double(context, 0);
	}
}

struct RtreeBoundingBoxData
{
	explicit RtreeBoundingBoxData(int32 InDimension)
	{
		check(InDimension >= 1);
		Bounds.Reset(2 * InDimension);
		for (int32 Dim = 0; Dim < InDimension; ++Dim)
		{
			Bounds.Emplace(std::numeric_limits<double>::max());
			Bounds.Emplace(std::numeric_limits<double>::lowest());
		}
	}

	bool IsValid() const { return Bounds[0] <= Bounds[1]; }
	int32 Dimension() const { return Bounds.Num() / 2; }
	TArray<double> Bounds;
};

/* This function will check through only the first level of nodes in the tree
* instead of iterating through the whole tree
*/
FORCEINLINE_DEBUGGABLE static int rtree_bbox_callback(sqlite3_rtree_query_info* info)
{
	// Validate that the context is setup
	if (!info->pContext)
	{
		UE_LOG(PointCloudLog, Log, TEXT("No context point in the bounding box query on Point Cloud"));
		return SQLITE_ERROR;
	}

	struct RtreeBoundingBoxData* Data = reinterpret_cast<RtreeBoundingBoxData*>(info->pContext);
	
	// Validate that the number of coordinates is what we're expecting
	if (info->nCoord != 2 * Data->Dimension())
	{
		UE_LOG(PointCloudLog, Log, TEXT("Point Cloud dimension (%d) does not match expected value (%d)"), info->nCoord / 2, Data->Dimension());
		return SQLITE_ERROR;
	}
	
	// Compute bounding box		
	for (int32 Dim = 0; Dim < Data->Dimension(); ++Dim)
	{
		int32 MinIndex = 2 * Dim + 0;
		int32 MaxIndex = 2 * Dim + 1;

		if (info->aCoord[MinIndex] < Data->Bounds[MinIndex])
			Data->Bounds[MinIndex] = info->aCoord[MinIndex];

		if (info->aCoord[MaxIndex] > Data->Bounds[MaxIndex])
			Data->Bounds[MaxIndex] = info->aCoord[MaxIndex];
	}

	// Set NOT_WITHIN to stop further descending into the r-tree
	info->eWithin = NOT_WITHIN;
	return SQLITE_OK;
}

FBox SQLExtension::query_rtree_bbox(sqlite3* db_handle, const char* rtree_name)
{
	/* attempting to query the BBOX of the R*Tree */
	const int32 Dimension = 3;
	RtreeBoundingBoxData Data(Dimension);

	char* callbackName = sqlite3_mprintf("rtree_bbox_%d", FPlatformTLS::GetCurrentThreadId());

	/* registering the Geometry Query Callback SQL function */
	sqlite3_rtree_query_callback(db_handle, callbackName,
		rtree_bbox_callback, &Data, nullptr);

	/* executing the SQL Query statement */
	char* sql = sqlite3_mprintf(
		"SELECT id FROM %s WHERE id MATCH %s(1)",
		rtree_name, callbackName);
	int ret = sqlite3_exec(db_handle, sql, nullptr, nullptr, nullptr);

	// Free temporary strings
	sqlite3_free(callbackName);
	sqlite3_free(sql);

	FBox Result(EForceInit::ForceInit);

	if (ret != SQLITE_OK)
	{
		UE_LOG(PointCloudLog, Log, TEXT("Bounding Box Error %s"), ANSI_TO_TCHAR(sqlite3_errmsg(db_handle)));
	}
	else if (!Data.IsValid())
	{
		UE_LOG(PointCloudLog, Log, TEXT("Empty Point Cloud"));
	}
	else
	{
		Result = FBox(FVector(Data.Bounds[0], Data.Bounds[2], Data.Bounds[4]), FVector(Data.Bounds[1], Data.Bounds[3], Data.Bounds[5]));

		// Fixup the max bound if there's a truncation from double to float
		if constexpr (!std::is_same_v<decltype(Result.Max.X), decltype(Data.Bounds[0])>)
		{
			while ((double)Result.Min.X > Data.Bounds[0])
				Result.Min.X = nextafterf(Result.Min.X, -1.0f);

			while ((double)Result.Max.X < Data.Bounds[1])
				Result.Max.X = nextafterf(Result.Max.X, +1.0f);

			while ((double)Result.Min.Y > Data.Bounds[2])
				Result.Min.Y = nextafterf(Result.Min.Y, -1.0f);

			while ((double)Result.Max.Y < Data.Bounds[3])
				Result.Max.Y = nextafterf(Result.Max.Y, +1.0f);

			while ((double)Result.Min.Z > Data.Bounds[4])
				Result.Min.Z = nextafterf(Result.Min.Z, -1.0f);

			while ((double)Result.Max.Z < Data.Bounds[5])
				Result.Max.Z = nextafterf(Result.Max.Z, +1.0f);
		}
	}

	return Result;
}

typedef sqlite3_uint64 u64;

/******************************************************************************
** The Hash Engine
*/
/*
** Macros to determine whether the machine is big or little endian,
** and whether or not that determination is run-time or compile-time.
**
** For best performance, an attempt is made to guess at the byte-order
** using C-preprocessor macros.  If that is unsuccessful, or if
** -DSHA3_BYTEORDER=0 is set, then byte-order is determined
** at run-time.
*/
#ifndef SHA3_BYTEORDER
# if defined(i386)     || defined(__i386__)   || defined(_M_IX86) ||    \
     defined(__x86_64) || defined(__x86_64__) || defined(_M_X64)  ||    \
     defined(_M_AMD64) || defined(_M_ARM)     || defined(__x86)   ||    \
     defined(__arm__)
#   define SHA3_BYTEORDER    1234
# elif defined(sparc)    || defined(__ppc__)
#   define SHA3_BYTEORDER    4321
# else
#   define SHA3_BYTEORDER 0
# endif
#endif

/*
** State structure for a SHA3 hash in progress
*/
typedef struct SHA3Context SHA3Context;
struct SHA3Context {
	union {
		u64 s[25];                /* Keccak state. 5x5 lines of 64 bits each */
		unsigned char x[1600];    /* ... or 1600 bytes */
	} u;
	unsigned nRate;        /* Bytes of input accepted per Keccak iteration */
	unsigned nLoaded;      /* Input bytes loaded into u.x[] so far this cycle */
	unsigned ixMask;       /* Insert next input into u.x[nLoaded^ixMask]. */
};

void SQLExtension::KeccakF1600Step(SHA3Context* p)
{
	int i;
	u64 b0, b1, b2, b3, b4;
	u64 c0, c1, c2, c3, c4;
	u64 d0, d1, d2, d3, d4;
	static const u64 RC[] = {
	  0x0000000000000001ULL,  0x0000000000008082ULL,
	  0x800000000000808aULL,  0x8000000080008000ULL,
	  0x000000000000808bULL,  0x0000000080000001ULL,
	  0x8000000080008081ULL,  0x8000000000008009ULL,
	  0x000000000000008aULL,  0x0000000000000088ULL,
	  0x0000000080008009ULL,  0x000000008000000aULL,
	  0x000000008000808bULL,  0x800000000000008bULL,
	  0x8000000000008089ULL,  0x8000000000008003ULL,
	  0x8000000000008002ULL,  0x8000000000000080ULL,
	  0x000000000000800aULL,  0x800000008000000aULL,
	  0x8000000080008081ULL,  0x8000000000008080ULL,
	  0x0000000080000001ULL,  0x8000000080008008ULL
	};
# define a00 (p->u.s[0])
# define a01 (p->u.s[1])
# define a02 (p->u.s[2])
# define a03 (p->u.s[3])
# define a04 (p->u.s[4])
# define a10 (p->u.s[5])
# define a11 (p->u.s[6])
# define a12 (p->u.s[7])
# define a13 (p->u.s[8])
# define a14 (p->u.s[9])
# define a20 (p->u.s[10])
# define a21 (p->u.s[11])
# define a22 (p->u.s[12])
# define a23 (p->u.s[13])
# define a24 (p->u.s[14])
# define a30 (p->u.s[15])
# define a31 (p->u.s[16])
# define a32 (p->u.s[17])
# define a33 (p->u.s[18])
# define a34 (p->u.s[19])
# define a40 (p->u.s[20])
# define a41 (p->u.s[21])
# define a42 (p->u.s[22])
# define a43 (p->u.s[23])
# define a44 (p->u.s[24])
# define ROL64(a,x) ((a<<x)|(a>>(64-x)))

	for (i = 0; i < 24; i += 4) {
		c0 = a00 ^ a10 ^ a20 ^ a30 ^ a40;
		c1 = a01 ^ a11 ^ a21 ^ a31 ^ a41;
		c2 = a02 ^ a12 ^ a22 ^ a32 ^ a42;
		c3 = a03 ^ a13 ^ a23 ^ a33 ^ a43;
		c4 = a04 ^ a14 ^ a24 ^ a34 ^ a44;
		d0 = c4 ^ ROL64(c1, 1);
		d1 = c0 ^ ROL64(c2, 1);
		d2 = c1 ^ ROL64(c3, 1);
		d3 = c2 ^ ROL64(c4, 1);
		d4 = c3 ^ ROL64(c0, 1);

		b0 = (a00 ^ d0);
		b1 = ROL64((a11 ^ d1), 44);
		b2 = ROL64((a22 ^ d2), 43);
		b3 = ROL64((a33 ^ d3), 21);
		b4 = ROL64((a44 ^ d4), 14);
		a00 = b0 ^ ((~b1) & b2);
		a00 ^= RC[i];
		a11 = b1 ^ ((~b2) & b3);
		a22 = b2 ^ ((~b3) & b4);
		a33 = b3 ^ ((~b4) & b0);
		a44 = b4 ^ ((~b0) & b1);

		b2 = ROL64((a20 ^ d0), 3);
		b3 = ROL64((a31 ^ d1), 45);
		b4 = ROL64((a42 ^ d2), 61);
		b0 = ROL64((a03 ^ d3), 28);
		b1 = ROL64((a14 ^ d4), 20);
		a20 = b0 ^ ((~b1) & b2);
		a31 = b1 ^ ((~b2) & b3);
		a42 = b2 ^ ((~b3) & b4);
		a03 = b3 ^ ((~b4) & b0);
		a14 = b4 ^ ((~b0) & b1);

		b4 = ROL64((a40 ^ d0), 18);
		b0 = ROL64((a01 ^ d1), 1);
		b1 = ROL64((a12 ^ d2), 6);
		b2 = ROL64((a23 ^ d3), 25);
		b3 = ROL64((a34 ^ d4), 8);
		a40 = b0 ^ ((~b1) & b2);
		a01 = b1 ^ ((~b2) & b3);
		a12 = b2 ^ ((~b3) & b4);
		a23 = b3 ^ ((~b4) & b0);
		a34 = b4 ^ ((~b0) & b1);

		b1 = ROL64((a10 ^ d0), 36);
		b2 = ROL64((a21 ^ d1), 10);
		b3 = ROL64((a32 ^ d2), 15);
		b4 = ROL64((a43 ^ d3), 56);
		b0 = ROL64((a04 ^ d4), 27);
		a10 = b0 ^ ((~b1) & b2);
		a21 = b1 ^ ((~b2) & b3);
		a32 = b2 ^ ((~b3) & b4);
		a43 = b3 ^ ((~b4) & b0);
		a04 = b4 ^ ((~b0) & b1);

		b3 = ROL64((a30 ^ d0), 41);
		b4 = ROL64((a41 ^ d1), 2);
		b0 = ROL64((a02 ^ d2), 62);
		b1 = ROL64((a13 ^ d3), 55);
		b2 = ROL64((a24 ^ d4), 39);
		a30 = b0 ^ ((~b1) & b2);
		a41 = b1 ^ ((~b2) & b3);
		a02 = b2 ^ ((~b3) & b4);
		a13 = b3 ^ ((~b4) & b0);
		a24 = b4 ^ ((~b0) & b1);

		c0 = a00 ^ a20 ^ a40 ^ a10 ^ a30;
		c1 = a11 ^ a31 ^ a01 ^ a21 ^ a41;
		c2 = a22 ^ a42 ^ a12 ^ a32 ^ a02;
		c3 = a33 ^ a03 ^ a23 ^ a43 ^ a13;
		c4 = a44 ^ a14 ^ a34 ^ a04 ^ a24;
		d0 = c4 ^ ROL64(c1, 1);
		d1 = c0 ^ ROL64(c2, 1);
		d2 = c1 ^ ROL64(c3, 1);
		d3 = c2 ^ ROL64(c4, 1);
		d4 = c3 ^ ROL64(c0, 1);

		b0 = (a00 ^ d0);
		b1 = ROL64((a31 ^ d1), 44);
		b2 = ROL64((a12 ^ d2), 43);
		b3 = ROL64((a43 ^ d3), 21);
		b4 = ROL64((a24 ^ d4), 14);
		a00 = b0 ^ ((~b1) & b2);
		a00 ^= RC[i + 1];
		a31 = b1 ^ ((~b2) & b3);
		a12 = b2 ^ ((~b3) & b4);
		a43 = b3 ^ ((~b4) & b0);
		a24 = b4 ^ ((~b0) & b1);

		b2 = ROL64((a40 ^ d0), 3);
		b3 = ROL64((a21 ^ d1), 45);
		b4 = ROL64((a02 ^ d2), 61);
		b0 = ROL64((a33 ^ d3), 28);
		b1 = ROL64((a14 ^ d4), 20);
		a40 = b0 ^ ((~b1) & b2);
		a21 = b1 ^ ((~b2) & b3);
		a02 = b2 ^ ((~b3) & b4);
		a33 = b3 ^ ((~b4) & b0);
		a14 = b4 ^ ((~b0) & b1);

		b4 = ROL64((a30 ^ d0), 18);
		b0 = ROL64((a11 ^ d1), 1);
		b1 = ROL64((a42 ^ d2), 6);
		b2 = ROL64((a23 ^ d3), 25);
		b3 = ROL64((a04 ^ d4), 8);
		a30 = b0 ^ ((~b1) & b2);
		a11 = b1 ^ ((~b2) & b3);
		a42 = b2 ^ ((~b3) & b4);
		a23 = b3 ^ ((~b4) & b0);
		a04 = b4 ^ ((~b0) & b1);

		b1 = ROL64((a20 ^ d0), 36);
		b2 = ROL64((a01 ^ d1), 10);
		b3 = ROL64((a32 ^ d2), 15);
		b4 = ROL64((a13 ^ d3), 56);
		b0 = ROL64((a44 ^ d4), 27);
		a20 = b0 ^ ((~b1) & b2);
		a01 = b1 ^ ((~b2) & b3);
		a32 = b2 ^ ((~b3) & b4);
		a13 = b3 ^ ((~b4) & b0);
		a44 = b4 ^ ((~b0) & b1);

		b3 = ROL64((a10 ^ d0), 41);
		b4 = ROL64((a41 ^ d1), 2);
		b0 = ROL64((a22 ^ d2), 62);
		b1 = ROL64((a03 ^ d3), 55);
		b2 = ROL64((a34 ^ d4), 39);
		a10 = b0 ^ ((~b1) & b2);
		a41 = b1 ^ ((~b2) & b3);
		a22 = b2 ^ ((~b3) & b4);
		a03 = b3 ^ ((~b4) & b0);
		a34 = b4 ^ ((~b0) & b1);

		c0 = a00 ^ a40 ^ a30 ^ a20 ^ a10;
		c1 = a31 ^ a21 ^ a11 ^ a01 ^ a41;
		c2 = a12 ^ a02 ^ a42 ^ a32 ^ a22;
		c3 = a43 ^ a33 ^ a23 ^ a13 ^ a03;
		c4 = a24 ^ a14 ^ a04 ^ a44 ^ a34;
		d0 = c4 ^ ROL64(c1, 1);
		d1 = c0 ^ ROL64(c2, 1);
		d2 = c1 ^ ROL64(c3, 1);
		d3 = c2 ^ ROL64(c4, 1);
		d4 = c3 ^ ROL64(c0, 1);

		b0 = (a00 ^ d0);
		b1 = ROL64((a21 ^ d1), 44);
		b2 = ROL64((a42 ^ d2), 43);
		b3 = ROL64((a13 ^ d3), 21);
		b4 = ROL64((a34 ^ d4), 14);
		a00 = b0 ^ ((~b1) & b2);
		a00 ^= RC[i + 2];
		a21 = b1 ^ ((~b2) & b3);
		a42 = b2 ^ ((~b3) & b4);
		a13 = b3 ^ ((~b4) & b0);
		a34 = b4 ^ ((~b0) & b1);

		b2 = ROL64((a30 ^ d0), 3);
		b3 = ROL64((a01 ^ d1), 45);
		b4 = ROL64((a22 ^ d2), 61);
		b0 = ROL64((a43 ^ d3), 28);
		b1 = ROL64((a14 ^ d4), 20);
		a30 = b0 ^ ((~b1) & b2);
		a01 = b1 ^ ((~b2) & b3);
		a22 = b2 ^ ((~b3) & b4);
		a43 = b3 ^ ((~b4) & b0);
		a14 = b4 ^ ((~b0) & b1);

		b4 = ROL64((a10 ^ d0), 18);
		b0 = ROL64((a31 ^ d1), 1);
		b1 = ROL64((a02 ^ d2), 6);
		b2 = ROL64((a23 ^ d3), 25);
		b3 = ROL64((a44 ^ d4), 8);
		a10 = b0 ^ ((~b1) & b2);
		a31 = b1 ^ ((~b2) & b3);
		a02 = b2 ^ ((~b3) & b4);
		a23 = b3 ^ ((~b4) & b0);
		a44 = b4 ^ ((~b0) & b1);

		b1 = ROL64((a40 ^ d0), 36);
		b2 = ROL64((a11 ^ d1), 10);
		b3 = ROL64((a32 ^ d2), 15);
		b4 = ROL64((a03 ^ d3), 56);
		b0 = ROL64((a24 ^ d4), 27);
		a40 = b0 ^ ((~b1) & b2);
		a11 = b1 ^ ((~b2) & b3);
		a32 = b2 ^ ((~b3) & b4);
		a03 = b3 ^ ((~b4) & b0);
		a24 = b4 ^ ((~b0) & b1);

		b3 = ROL64((a20 ^ d0), 41);
		b4 = ROL64((a41 ^ d1), 2);
		b0 = ROL64((a12 ^ d2), 62);
		b1 = ROL64((a33 ^ d3), 55);
		b2 = ROL64((a04 ^ d4), 39);
		a20 = b0 ^ ((~b1) & b2);
		a41 = b1 ^ ((~b2) & b3);
		a12 = b2 ^ ((~b3) & b4);
		a33 = b3 ^ ((~b4) & b0);
		a04 = b4 ^ ((~b0) & b1);

		c0 = a00 ^ a30 ^ a10 ^ a40 ^ a20;
		c1 = a21 ^ a01 ^ a31 ^ a11 ^ a41;
		c2 = a42 ^ a22 ^ a02 ^ a32 ^ a12;
		c3 = a13 ^ a43 ^ a23 ^ a03 ^ a33;
		c4 = a34 ^ a14 ^ a44 ^ a24 ^ a04;
		d0 = c4 ^ ROL64(c1, 1);
		d1 = c0 ^ ROL64(c2, 1);
		d2 = c1 ^ ROL64(c3, 1);
		d3 = c2 ^ ROL64(c4, 1);
		d4 = c3 ^ ROL64(c0, 1);

		b0 = (a00 ^ d0);
		b1 = ROL64((a01 ^ d1), 44);
		b2 = ROL64((a02 ^ d2), 43);
		b3 = ROL64((a03 ^ d3), 21);
		b4 = ROL64((a04 ^ d4), 14);
		a00 = b0 ^ ((~b1) & b2);
		a00 ^= RC[i + 3];
		a01 = b1 ^ ((~b2) & b3);
		a02 = b2 ^ ((~b3) & b4);
		a03 = b3 ^ ((~b4) & b0);
		a04 = b4 ^ ((~b0) & b1);

		b2 = ROL64((a10 ^ d0), 3);
		b3 = ROL64((a11 ^ d1), 45);
		b4 = ROL64((a12 ^ d2), 61);
		b0 = ROL64((a13 ^ d3), 28);
		b1 = ROL64((a14 ^ d4), 20);
		a10 = b0 ^ ((~b1) & b2);
		a11 = b1 ^ ((~b2) & b3);
		a12 = b2 ^ ((~b3) & b4);
		a13 = b3 ^ ((~b4) & b0);
		a14 = b4 ^ ((~b0) & b1);

		b4 = ROL64((a20 ^ d0), 18);
		b0 = ROL64((a21 ^ d1), 1);
		b1 = ROL64((a22 ^ d2), 6);
		b2 = ROL64((a23 ^ d3), 25);
		b3 = ROL64((a24 ^ d4), 8);
		a20 = b0 ^ ((~b1) & b2);
		a21 = b1 ^ ((~b2) & b3);
		a22 = b2 ^ ((~b3) & b4);
		a23 = b3 ^ ((~b4) & b0);
		a24 = b4 ^ ((~b0) & b1);

		b1 = ROL64((a30 ^ d0), 36);
		b2 = ROL64((a31 ^ d1), 10);
		b3 = ROL64((a32 ^ d2), 15);
		b4 = ROL64((a33 ^ d3), 56);
		b0 = ROL64((a34 ^ d4), 27);
		a30 = b0 ^ ((~b1) & b2);
		a31 = b1 ^ ((~b2) & b3);
		a32 = b2 ^ ((~b3) & b4);
		a33 = b3 ^ ((~b4) & b0);
		a34 = b4 ^ ((~b0) & b1);

		b3 = ROL64((a40 ^ d0), 41);
		b4 = ROL64((a41 ^ d1), 2);
		b0 = ROL64((a42 ^ d2), 62);
		b1 = ROL64((a43 ^ d3), 55);
		b2 = ROL64((a44 ^ d4), 39);
		a40 = b0 ^ ((~b1) & b2);
		a41 = b1 ^ ((~b2) & b3);
		a42 = b2 ^ ((~b3) & b4);
		a43 = b3 ^ ((~b4) & b0);
		a44 = b4 ^ ((~b0) & b1);
	}
}

void SQLExtension::SHA3Init(SHA3Context* p, int iSize)
{
	memset(p, 0, sizeof(*p));
	if (iSize >= 128 && iSize <= 512) {
		p->nRate = (1600 - ((iSize + 31) & ~31) * 2) / 8;
	}
	else {
		p->nRate = (1600 - 2 * 256) / 8;
	}
#if SHA3_BYTEORDER==1234
	/* Known to be little-endian at compile-time. No-op */
#elif SHA3_BYTEORDER==4321
	p->ixMask = 7;  /* Big-endian */
#else
	{
		static unsigned int one = 1;
		if (1 == *(unsigned char*)&one) {
			/* Little endian.  No byte swapping. */
			p->ixMask = 0;
		}
		else {
			/* Big endian.  Byte swap. */
			p->ixMask = 7;
		}
	}
#endif
}

#ifdef __clang__
#pragma diagnostic push
#pragma clang diagnostic ignored "-Wnull-pointer-subtraction"
#endif
void SQLExtension::SHA3Update(SHA3Context* p, const unsigned char* aData, unsigned int nData)
{
	unsigned int i = 0;
#if SHA3_BYTEORDER==1234
	if ((p->nLoaded % 8) == 0 && ((aData - (const unsigned char*)0) & 7) == 0) {
		for (; i + 7 < nData; i += 8) {
			p->u.s[p->nLoaded / 8] ^= *(u64*)&aData[i];
			p->nLoaded += 8;
			if (p->nLoaded >= p->nRate) {
				KeccakF1600Step(p);
				p->nLoaded = 0;
			}
		}
	}
#endif
	for (; i < nData; i++) {
#if SHA3_BYTEORDER==1234
		p->u.x[p->nLoaded] ^= aData[i];
#elif SHA3_BYTEORDER==4321
		p->u.x[p->nLoaded ^ 0x07] ^= aData[i];
#else
		p->u.x[p->nLoaded ^ p->ixMask] ^= aData[i];
#endif
		p->nLoaded++;
		if (p->nLoaded == p->nRate) {
			KeccakF1600Step(p);
			p->nLoaded = 0;
		}
	}
}
#ifdef __clang__
#pragma diagnostic pop
#endif

unsigned char* SQLExtension::SHA3Final(SHA3Context* p)
{
	unsigned int i;
	if (p->nLoaded == p->nRate - 1) {
		const unsigned char c1 = 0x86;
		SHA3Update(p, &c1, 1);
	}
	else {
		const unsigned char c2 = 0x06;
		const unsigned char c3 = 0x80;
		SHA3Update(p, &c2, 1);
		p->nLoaded = p->nRate - 1;
		SHA3Update(p, &c3, 1);
	}
	for (i = 0; i < p->nRate; i++) {
		p->u.x[i + p->nRate] = p->u.x[i ^ p->ixMask];
	}
	return &p->u.x[p->nRate];
}

void SQLExtension::sha3Func(sqlite3_context* context, int argc, sqlite3_value** argv)
{
	SHA3Context cx;
	int eType = sqlite3_value_type(argv[0]);
	int nByte = sqlite3_value_bytes(argv[0]);
	int iSize;
	if (argc == 1) {
		iSize = 256;
	}
	else {
		iSize = sqlite3_value_int(argv[1]);
		if (iSize != 224 && iSize != 256 && iSize != 384 && iSize != 512) {
			sqlite3_result_error(context, "SHA3 size should be one of: 224 256 "
				"384 512", -1);
			return;
		}
	}
	if (eType == SQLITE_NULL) return;
	SHA3Init(&cx, iSize);
	if (eType == SQLITE_BLOB) {
		SHA3Update(&cx, (const unsigned char*)sqlite3_value_blob(argv[0]), nByte);
	}
	else {
		SHA3Update(&cx, sqlite3_value_text(argv[0]), nByte);
	}
	sqlite3_result_blob(context, SHA3Final(&cx), iSize / 8, SQLITE_TRANSIENT);
}

void SQLExtension::hash_step_vformat(SHA3Context* p, const char* zFormat, ...)
{
	va_list ap;
	int n;
	char zBuf[50];
	va_start(ap, zFormat);
	sqlite3_vsnprintf(sizeof(zBuf), zBuf, zFormat, ap);
	va_end(ap);
	n = (int)strlen(zBuf);
	SHA3Update(p, (unsigned char*)zBuf, n);
}

void SQLExtension::sha3QueryFunc(sqlite3_context* context, int argc, sqlite3_value** argv)
{
	sqlite3* db = sqlite3_context_db_handle(context);
	const char* zSql = (const char*)sqlite3_value_text(argv[0]);
	sqlite3_stmt* pStmt = 0;
	int nCol;                   /* Number of columns in the result set */
	int i;                      /* Loop counter */
	int rc;
	int n;
	const char* z;
	SHA3Context cx;
	int iSize;

	if (argc == 1) {
		iSize = 256;
	}
	else {
		iSize = sqlite3_value_int(argv[1]);
		if (iSize != 224 && iSize != 256 && iSize != 384 && iSize != 512) {
			sqlite3_result_error(context, "SHA3 size should be one of: 224 256 "
				"384 512", -1);
			return;
		}
	}
	if (zSql == 0) return;
	SHA3Init(&cx, iSize);
	while (zSql[0]) {
		rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, &zSql);
		if (rc) {
			char* zMsg = sqlite3_mprintf("error SQL statement [%s]: %s",
				zSql, sqlite3_errmsg(db));
			sqlite3_finalize(pStmt);
			sqlite3_result_error(context, zMsg, -1);
			sqlite3_free(zMsg);
			return;
		}
		if (!sqlite3_stmt_readonly(pStmt)) {
			char* zMsg = sqlite3_mprintf("non-query: [%s]", sqlite3_sql(pStmt));
			sqlite3_finalize(pStmt);
			sqlite3_result_error(context, zMsg, -1);
			sqlite3_free(zMsg);
			return;
		}
		nCol = sqlite3_column_count(pStmt);
		z = sqlite3_sql(pStmt);
		n = (int)strlen(z);

		bool bIncludeQuery = true;
		if (argc == 3)
		{
			bIncludeQuery = (bool)sqlite3_value_int(argv[2]);
		}

		if (bIncludeQuery) // include query in hash
		{
			hash_step_vformat(&cx, "S%d:", n);
			SHA3Update(&cx, (unsigned char*)z, n);
		}

		/* Compute a hash over the result of the query */
		while (SQLITE_ROW == sqlite3_step(pStmt)) {
			SHA3Update(&cx, (const unsigned char*)"R", 1);
			for (i = 0; i < nCol; i++) {
				switch (sqlite3_column_type(pStmt, i)) {
				case SQLITE_NULL: {
					SHA3Update(&cx, (const unsigned char*)"N", 1);
					break;
				}
				case SQLITE_INTEGER: {
					sqlite3_uint64 u;
					int j;
					unsigned char x[9];
					sqlite3_int64 v = sqlite3_column_int64(pStmt, i);
					memcpy(&u, &v, 8);
					for (j = 8; j >= 1; j--) {
						x[j] = u & 0xff;
						u >>= 8;
					}
					x[0] = 'I';
					SHA3Update(&cx, x, 9);
					break;
				}
				case SQLITE_FLOAT: {
					sqlite3_uint64 u;
					int j;
					unsigned char x[9];
					double r = sqlite3_column_double(pStmt, i);
					memcpy(&u, &r, 8);
					for (j = 8; j >= 1; j--) {
						x[j] = u & 0xff;
						u >>= 8;
					}
					x[0] = 'F';
					SHA3Update(&cx, x, 9);
					break;
				}
				case SQLITE_TEXT: {
					int n2 = sqlite3_column_bytes(pStmt, i);
					const unsigned char* z2 = sqlite3_column_text(pStmt, i);
					hash_step_vformat(&cx, "T%d:", n2);
					SHA3Update(&cx, z2, n2);
					break;
				}
				case SQLITE_BLOB: {
					int n2 = sqlite3_column_bytes(pStmt, i);
					const unsigned char* z2 = (const unsigned char*)sqlite3_column_blob(pStmt, i);
					hash_step_vformat(&cx, "B%d:", n2);
					SHA3Update(&cx, z2, n2);
					break;
				}
				}
			}
		}
		sqlite3_finalize(pStmt);
	}
	sqlite3_result_blob(context, SHA3Final(&cx), iSize / 8, SQLITE_TRANSIENT);
}

#undef SHA3_BYTEORDER
#undef a00
#undef a01
#undef a02
#undef a03
#undef a04
#undef a10
#undef a11
#undef a12
#undef a13
#undef a14
#undef a20
#undef a21
#undef a22
#undef a23
#undef a24
#undef a30
#undef a31
#undef a32
#undef a33
#undef a34
#undef a40
#undef a41
#undef a42
#undef a43
#undef a44
#undef ROL64

int SQLExtension::Sha3CallBack(void* UsrData, int argc, char** argv, char** azColName)
{
	if (UsrData == nullptr)
	{
		UE_LOG(PointCloudLog, Warning, TEXT("Null Array Passed"));
		return 0;
	}

	FString* HashString = (FString*)UsrData;

	*HashString = FString::FromHexBlob((const uint8*)argv[0], 32);

	return 0;
}