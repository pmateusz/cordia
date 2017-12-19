SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT aom.LtAomId AS 'aom_id', aom.AreaNo AS 'area_no', aom.LocationID AS 'location_id', aom.AreaCode AS 'area_code'
  FROM Homecare.dbo.ltAom aom
  WHERE Archived != 'Y'
   AND IgnoreInDW = 0
   AND Surname NOT IN ('TASS','Meals At Home','Overnight Service')
   AND ltRegionalManagerId IS NOT NULL
 ORDER BY area_no
