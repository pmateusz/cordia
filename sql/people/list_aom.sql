-- list available aoms

USE People;
SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT aom_details.AomCode AS 'aom_code',
	aom_details.AreaCode as 'area_code',
	aom_details.AreaNo as 'area_no',
	aom_details.Name as 'primary_manager',
	aom_area.AreaName as 'area_name'
  FROM dbo.AomDetails AS aom_details
       INNER JOIN dbo.LtAomArea AS aom_area
       ON aom_details.AomCode = aom_area.AomCode
 WHERE aom_details.AreaCode IS NOT NULL
 ORDER BY area_code, area_no
