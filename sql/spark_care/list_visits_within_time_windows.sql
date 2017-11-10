-- Lists all visits requested within the specified time interval and within the specified area

SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @VISITS_FROM DATETIME, @VISITS_TO DATETIME;
SET @VISITS_FROM = '2017-10-31';
SET @VISITS_TO = '2017-11-30';

DECLARE @AOM_CODE INT;
SET @AOM_CODE = 617;

SELECT visit.VisitID AS 'visit_id',
	   visit.ServiceUserID AS 'service_user_id',
	   visit.VisitDate AS 'visit_date',
	   visit.RequestedVisitTime AS 'requested_visit_time',
	   visit.RequestedVisitDuration AS 'requested_visit_duration',
	   service_user_view.AomID AS 'aom_id',
	   service_user_view.Street AS 'street',
	   service_user_view.Town AS 'town',
	   service_user_view.Postcode AS 'post_code'
  FROM SparkCare.dbo.Visit AS visit
	   INNER JOIN SparkCare.dbo.ServiceUserView AS service_user_view
	   ON visit.ServiceUserID = service_user_view.ServiceUserID
	   INNER JOIN SparkCare.dbo.AomView AS aom_view
	   ON service_user_view.AomID = aom_view.AomID
	   LEFT OUTER JOIN SparkCare.dbo.ServiceType AS service_type
       ON visit.ServiceTypeID = service_type.ServiceTypeID
       LEFT OUTER JOIN SparkCare.dbo.VisitType AS visit_type
       ON visit.VisitTypeID = visit_type.VisitTypeID
 WHERE visit.VisitDate >= @VISITS_FROM AND visit.VisitDate < @VISITS_TO
		AND aom_view.AomID = @AOM_CODE
		AND visit.Suspended = 0
    
