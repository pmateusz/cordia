DECLARE @CARER_ID INT;
SET @CARER_ID = 1574;

SELECT *
  FROM SparkCare.dbo.VisitAssignment visit
 WHERE visit.PlannedCarerID = @CARER_ID
 ORDER BY visit.PlannedStartDateTime
