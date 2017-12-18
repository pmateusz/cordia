-- list available aoms

SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT aom.AreaCode as 'area_code', aom.AreaNo as 'area_no', aom.LtAomId as 'aom_id'
  FROM Homecare.dbo.ltAom aom
  WHERE Archived != 'Y'
   AND IgnoreInDW = 0
   AND Surname NOT IN ('TASS','Meals At Home','Overnight Service')
   AND ltRegionalManagerId IS not null
 ORDER BY area_no
