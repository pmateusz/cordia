USE SparkCare;
SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT aom_sector.AomID as aom_id,
       sector.SectorName as sector_name,
       base.BaseName as base_name
  FROM dbo.AomSector AS aom_sector
       INNER JOIN dbo.Sector AS sector
       ON aom_sector.SectorID = sector.SectorID
       INNER JOIN dbo.AomBase AS aom_base
       ON aom_sector.AomID = aom_base.AomID
       INNER JOIN dbo.Base AS base
       ON aom_base.BaseID = base.BaseID
 ORDER BY sector_name, base_name
