--- Seems that the table dbo.AomAssignedShifts is not used is that correct?

USE People;
SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

SELECT *
  FROM dbo.AomAssignedShifts AS assigned_shifts
 WHERE assigned_shifts.NoOfPositions > 0
