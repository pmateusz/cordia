USE SparkCare;
SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @begin DATE, @end DATE;
SET @begin = '2017-10-25';
SET @end = '2017-10-31';

SELECT *
  FROM dbo.Visit AS visit
       INNER JOIN dbo.VisitType AS visit_type
       ON visit.VisitTypeID = visit_type.VisitTypeID
       LEFT OUTER JOIN dbo.VisitAssignment AS visit_assignment
       ON visit_assignment.VisitID = visit.VisitID
 WHERE visit.VisitDate >= @begin AND visit.VisitDate <= @end
