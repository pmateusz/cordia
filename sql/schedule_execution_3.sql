SELECT subquery.visit_id, subquery.carer_id
FROM (
SELECT DISTINCT visits.visit_id, visits.time as 'requested_time',
	visits.duration as 'requested_duration',
	CONVERT(time, carer_visits.PlannedStartDateTime) as 'planned_time',
	DATEDIFF(minute, carer_visits.PlannedStartDateTime,carer_visits.PlannedEndDateTime) as 'planned_duration',
	CONVERT(time, carer_visits.CheckInDateTime) as 'real_start_time',
	DATEDIFF(minute, carer_visits.CheckInDateTime, carer_visits.CheckOutDateTime) as 'real_duration',
	CAST(ROUND(dbo.CalculateDuration(CAST(CONVERT(date, carer_visits.CheckInDateTime) as datetime) + cast(visits.time as datetime), carer_visits.CheckInDateTime) / 60.0, 0) as int) as 'delay_wrt_required',
		CAST(ROUND(dbo.CalculateDuration(carer_visits.PlannedStartDateTime, carer_visits.CheckInDateTime) / 60.0, 0) as int) as 'delay_wrt_planned',
	carer_visits.PlannedCarerID as 'carer_id',
	employees.is_mobile_worker,
	employees.is_agency_carer,
	(SELECT COUNT(intervals.CarerId) FROM dbo.ListCarerIntervals intervals WHERE CONVERT(date, intervals.StartDateTime) = '3/30/2017' AND intervals.CarerId = employees.carer_id) as work_periods
FROM
(
	SELECT visit_orders.visit_id as 'visit_id',
		STRING_AGG(visit_orders.task, ',') WITHIN GROUP (ORDER BY visit_orders.task) as 'tasks',
		MIN(visit_orders.visit_time) as 'time',
		MIN(visit_orders.visit_duration) as 'duration'
	FROM (
		SELECT DISTINCT visit_window.visit_id as visit_id,
			CONVERT(int, visit_window.task_no) as task,
			MIN(visit_window.service_user_id) as service_user_id, 
			MIN(visit_window.requested_visit_time) as visit_time,
			MIN(visit_window.requested_visit_duration) as visit_duration
		FROM dbo.ListVisitsWithinWindow visit_window
		INNER JOIN dbo.ListAom aom
		ON aom.aom_id = visit_window.aom_code
		WHERE aom.area_code = 'C240' AND visit_window.visit_date = '3/30/2017'
		GROUP BY visit_window.visit_id, visit_window.task_no
	
	) visit_orders
	GROUP BY visit_orders.visit_id
) visits
LEFT OUTER JOIN dbo.ListCarerVisits carer_visits
ON visits.visit_id = carer_visits.VisitID
LEFT OUTER JOIN dbo.ListEmployees employees
ON carer_visits.PlannedCarerID = employees.carer_id) subquery
WHERE subquery.work_periods = 0