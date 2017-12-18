SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

DECLARE @START_TIME DATETIME, @END_TIME DATETIME;
SET @START_TIME = '2017-10-31';
SET @END_TIME = '2017-11-30';

DECLARE @AOM_ID INT;
SET @AOM_ID = 627;

SELECT carer_view.CarerId AS 'carer_id',
	CASE
		WHEN carer_view.HiredDate < @START_TIME THEN @START_TIME
		ELSE carer_view.HiredDate
	END AS 'start_time',
	CASE
		WHEN carer_view.TerminatedDate IS NOT NULL AND carer_view.TerminatedDate < @END_TIME THEN carer_view.TerminatedDate
		ELSE COALESCE(carer_view.TerminatedDate, @END_TIME)
	END AS 'end_time',
	carer_view.AomId AS 'aom_id',
	PositionHours as 'position_hours',
	BackToBackCarerID as 'back_to_back_carer',
	PartnerCarerID as 'partner_carer',
	IsMobileWorker as 'is_mobile_worker',
	IsVehicular as 'is_vehicular',
	IsAgencyCarer as 'is_agency_carer'
  FROM SparkCare.dbo.CarerView AS carer_view
 WHERE carer_view.HiredDate < @END_TIME
       AND (carer_view.TerminatedDate IS NULL or carer_view.TerminatedDate > @START_TIME)
       AND (carer_view.AomId = @AOM_ID)
 ORDER BY carer_id, start_time
