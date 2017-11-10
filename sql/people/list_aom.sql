-- list available aoms

SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT aom_details.AomCode AS 'aom_code',
       aom_base.AomId AS 'aom_id',
	   aom_details.AreaCode as 'area_code',
	   aom_details.AreaNo as 'area_no',
	   aom_details.Name as 'primary_manager'
  FROM People.dbo.AomDetails AS aom_details
	   INNER JOIN SparkCare.dbo.AomBase as aom_base
	   ON aom_base.AomBaseID = aom_details.AomCode
 WHERE aom_details.AreaCode IS NOT NULL
 ORDER BY aom_code, area_no
