// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace PointCloud
{	
	static const FString SchemaQuery = R"SchmStrnLtr(

		DROP TABLE if exists Vertex;
		DROP TABLE if exists SpatialQuery;
		DROP TABLE if exists Object;
		DROP TABLE if exists AttributeKeys;
		DROP TABLE if exists AttributeValues;
		DROP TABLE if exists VertexToAttribute;

		PRAGMA synchronous = OFF;
		PRAGMA journal_mode = MEMORY;
		PRAGMA page_size = 8096;
		PRAGMA encoding = 'UTF-8';
		PRAGMA user_version = 2;

		CREATE TABLE if not exists Vertex (ObjectId INTEGER, 
				x REAL, 
				y REAL, 
				z REAL, 
				nx REAL, 
				ny REAL, 
				nz REAL, 
				nw REAL, 
				u REAL, 
				v REAL,
				sx REAL, 
				sy REAL, 
				sz REAL);

		CREATE TABLE AttributeKeys( Name STRING UNIQUE);

		CREATE TABLE AttributeValues(
			Value TEXT NOT NULL	UNIQUE
		); 

		CREATE TABLE VertexToAttribute(
			vertex_id  	INTEGER NOT NULL, 
			key_id  	INTEGER NOT NULL, 
			value_id 	INTEGER NOT NULL
		); 

		CREATE TABLE if not exists Object (Name STRING UNIQUE);

		CREATE VIEW MetaData AS SELECT VertexToAttribute.vertex_id As Vertex_Id, AttributeKeys.Name As Attribute_Name, AttributeValues.Value As Attribute_Value FROM AttributeValues INNER JOIN VertexToAttribute ON AttributeValues.rowid = VertexToAttribute.value_id INNER JOIN AttributeKeys ON AttributeKeys.rowid = VertexToAttribute.key_id;
		;)SchmStrnLtr";

		//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const FString ConvertFromSchemaOneToTwoQuery = R"SchmStrnLtr(
		BEGIN TRANSACTION;

		DROP TABLE IF EXISTS 	VertexToAttribute2;
		DROP TABLE IF EXISTS 	AttributeValues2;
		DROP VIEW  IF EXISTS 	MetaData2;

		DROP INDEX IF EXISTS 	KeytoValue;
		DROP INDEX IF EXISTS 	VertexToValue;
		DROP INDEX IF EXISTS 	VertexToKey;

		CREATE TABLE AttributeValues2(
			Value TEXT NOT NULL	UNIQUE
		);

		INSERT INTO AttributeValues2(Value) SELECT DISTINCT AttributeValues.Value AS Value from AttributeValues;

		CREATE TABLE VertexToAttribute2(
			vertex_id  INTEGER NOT NULL,
			key_id  	INTEGER NOT NULL,
			value_id 	INTEGER NOT NULL
		);

		INSERT INTO VertexToAttribute2 SELECT VertexToAttribute.vertex_id AS vertex_id, AttributeValues.key_id as key_id, AttributeValues2.rowid As value_id FROM AttributeValues2 INNER JOIN AttributeValues ON AttributeValues2.Value = AttributeValues.Value INNER JOIN VertexToAttribute ON VertexToAttribute.attribute_id = AttributeValues.rowid;--SELECT VertexToAttribute.vertex_id AS vertex_id, AttributeValues.key_id as key_id, AttributeValues2.rowid As value_id FROM AttributeValues2 INNER JOIN AttributeValues ON AttributeValues2.Value = AttributeValues.Value INNER JOIN VertexToAttribute ON VertexToAttribute.attribute_id = AttributeValues.rowid;--SELECT VertexToAttribute.vertex_id AS vertex_id, AttributeValues.key_id as key_id, AttributeValues2.rowid As value_id FROM AttributeValues2 INNER JOIN AttributeValues ON AttributeValues2.Value = AttributeValues.Value INNER JOIN VertexToAttribute ON VertexToAttribute.attribute_id = AttributeValues.rowid;

		CREATE INDEX KeytoValue 	ON VertexToAttribute2(key_id, value_id);
		CREATE INDEX VertexToValue 	ON VertexToAttribute2(vertex_id, value_id);
		CREATE INDEX VertexToKey 	ON VertexToAttribute2(vertex_id, key_id);

		DROP TABLE IF EXISTS VertexToAttribute;
		DROP TABLE IF EXISTS AttributeValues;
		DROP VIEW IF EXISTS MetaData;

		ALTER TABLE AttributeValues2 RENAME TO AttributeValues;
		ALTER TABLE VertexToAttribute2 RENAME TO VertexToAttribute;

		CREATE VIEW MetaData AS SELECT VertexToAttribute.vertex_id As Vertex_Id, AttributeKeys.Name As Attribute_Name, AttributeValues.Value As Attribute_Value FROM AttributeValues INNER JOIN VertexToAttribute ON AttributeValues.rowid = VertexToAttribute.value_id INNER JOIN AttributeKeys ON AttributeKeys.rowid = VertexToAttribute.key_id;

		END TRANSACTION;

		PRAGMA user_version = 2;

		VACUUM;
		ANALYZE;)SchmStrnLtr";
}



