-- Lists all visits requested within the specified time interval and within the specified area

SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @START_TIME DATETIME, @END_TIME DATETIME;
SET @START_TIME = '2017-10-31';
SET @END_TIME = '2017-11-30';

DECLARE @AOM_CODE INT;
SET @AOM_CODE = 1;

SELECT TOP(100) visit.VisitID AS 'visit_id',
       visit.ServiceUserID AS 'service_user_id',
       CAST(visit.VisitDate AS DATE) AS 'visit_date',
       visit.RequestedVisitTime AS 'requested_visit_time',
       visit.RequestedVisitDuration AS 'requested_visit_duration',
       service_user_view.Street AS 'street',
       service_user_view.Town AS 'town',
       service_user_view.Postcode AS 'post_code',
       aom.LtAomId AS 'aom_code'
  FROM SparkCare.dbo.Visit AS visit
       INNER JOIN SparkCare.dbo.ServiceUserView AS service_user_view
       ON visit.ServiceUserID = service_user_view.ServiceUserID
       LEFT OUTER JOIN Homecare.dbo.ltAom aom
       ON aom.LtAomId = service_user_view.AomID
       LEFT OUTER JOIN SparkCare.dbo.ServiceType AS service_type
       ON visit.ServiceTypeID = service_type.ServiceTypeID
       LEFT OUTER JOIN SparkCare.dbo.VisitType AS visit_type
       ON visit.VisitTypeID = visit_type.VisitTypeID
 WHERE visit.VisitDate BETWEEN @START_TIME AND @END_TIME
       AND aom.LtAomId = @AOM_CODE
       AND visit.Suspended = 0
