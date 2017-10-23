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
       LEFT OUTER JOIN dbo.AomCarer aom_carer
       ON aom_base.AomID = aom_carer.AomId
 WHERE aom_carer.AomCarerId IS NULL
 
 -- There are 2 areas without a carer
