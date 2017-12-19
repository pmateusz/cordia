SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @AOM_ID INT;
SET @AOM_ID = 637;

SELECT *
  FROM SparkCare.dbo.CarerIntervalsView carer_intervals
  INNER JOIN People.dbo.IntervalType interval
    ON carer_intervals.IntervalType = interval.IntervalTypeID
 WHERE carer_intervals.AomID = @AOM_ID
 ORDER BY carer_intervals.StartDateTime
