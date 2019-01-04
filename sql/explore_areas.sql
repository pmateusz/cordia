-- find for each area: unique visits, unique clients, first visit, last visit
DECLARE @VisitTasks TABLE (
	VisitId INT NOT NULL,
	ClientId INT NOT NULL,
	AomId INT NOT NULL,
	Tasks VARCHAR(64) NOT NULL
);

INSERT INTO @VisitTasks(VisitId, ClientId, AomId, Tasks)
SELECT DISTINCT inner_visits.visit_id, MIN(inner_visits.service_user_id), MIN(inner_visits.aom_code), STRING_AGG(inner_visits.tasks, ';')  WITHIN GROUP (ORDER BY inner_visits.tasks)
FROM (
	SELECT visit_id, service_user_id, aom_code, CONVERT(int, task_no) as tasks
	FROM dbo.ListVisitsWithinWindow
) AS inner_visits
GROUP BY inner_visits.visit_id

SELECT MIN(aom.area_code), COUNT(VisitId) as 'Visits'
FROM @VisitTasks
INNER JOIN ListAom aom
ON aom.aom_id = AomId
GROUP BY AomId
ORDER BY 'Visits' DESC

SELECT Clients.AomId as 'area', COUNT(Clients.ClientId) as 'clients'
FROM (SELECT DISTINCT aom.area_code as AomId, service_user_id as ClientId
	FROM dbo.ListVisitsWithinWindow visits
	INNER JOIN dbo.ListAom aom
	ON visits.aom_code = aom.aom_id) Clients
GROUP BY Clients.AomId
ORDER BY 'clients' DESC

SELECT MIN(aom.area_code) as 'area', COUNT(carer_visits.VisitID) as 'visits', MIN(carer_visits.PlannedStartDateTime) as 'first_visit', MAX(carer_visits.PlannedStartDateTime) as 'last_visit'
FROM dbo.ListCarerVisits carer_visits
INNER JOIN dbo.ListVisitsWithinWindow window_visits
ON carer_visits.VisitID = window_visits.visit_id
INNER JOIN dbo.ListAom aom
ON aom.aom_id = window_visits.aom_code
GROUP BY aom.aom_id
ORDER BY 'visits' DESC

-- get list dataset
DECLARE @area VARCHAR(10) = 'C120';

SELECT DISTINCT window_visits.visit_id as 'visit_id', window_visits.service_user_id as 'client_id',
		task_visits.Tasks as 'tasks',
		aom.area_code as 'area',
		carer_visits.PlannedCarerID as 'carer',
		carer_visits.PlannedStartDateTime as 'planned_start_time',
		carer_visits.PlannedEndDateTime as 'planned_end_time',
		carer_visits.CheckInDateTime as 'check_in',
		carer_visits.CheckOutDateTime as 'check_out',
		carer_visits.AssumedCheckInProcessed as 'check_in_processed'
FROM dbo.ListCarerVisits carer_visits
INNER JOIN dbo.ListVisitsWithinWindow window_visits
ON carer_visits.VisitID = window_visits.visit_id
INNER JOIN dbo.ListAom aom
ON aom.aom_id = window_visits.aom_code
INNER JOIN @VisitTasks task_visits
ON carer_visits.VisitID = task_visits.VisitId
WHERE aom.area_code = @area