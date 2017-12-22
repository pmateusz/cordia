SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
GO

-- create temp table containing all dates within window....
SELECT DISTINCT CAST(PlannedStartDateTime AS DATE) AS [Date]
INTO #DateRange
FROM SparkCare.dbo.VisitAssignment
WHERE CAST(PlannedStartDateTime AS DATE) BETWEEN '20170101' AND '20171231'
ORDER BY [Date]


-- create temp table containing all carer interval (including agreed overtime) within window
-- this is based on the carerintervalsview.........
SELECT *
INTO #CarerIntervals
FROM (
       SELECT
       iv.Type AS IntervalType
	   ,CAST(CAST(iv.StartTime AS TIME) AS DATETIME) + CAST(CAST(dr.[Date] AS DATE)AS DATETIME) AS StartDateTime
   	   ,CAST(CAST(iv.EndTime AS TIME) AS DATETIME) + CAST(CAST(dr.[Date] AS DATE) AS DATETIME) AS EndDateTime
	   ,cv.CarerID
	   ,cv.AomID
	   , NULL AS OT
       FROM #DateRange dr
	   CROSS JOIN SparkCare.dbo.CarerView cv WITH(NOLOCK)
       INNER JOIN [People].dbo.OverrideSchedule os WITH(NOLOCK) ON os.EmployeePositionID = cv.CarerID
       INNER JOIN [People].dbo.SchedulePattern sp WITH(NOLOCK) ON sp.SchedulePatternID = os.SchedulePatternID
       INNER JOIN [People].dbo.WeekSlot wk WITH(NOLOCK) ON wk.SchedulePatternID = sp.SchedulePatternID AND wk.WeekNumber = People.dbo.GetActiveWeekForShiftPattern(sp.StartDate, dr.[Date], (SELECT COUNT(*) FROM [People].dbo.WeekSlot WHERE WeekSlot.SchedulePatternID=sp.SchedulePatternID), sp.StartingWeek)
       INNER JOIN [People].dbo.IntervalSlot iv WITH(NOLOCK) ON iv.WeekSlotId = wk.WeekSlotID AND iv.Day = DATEPART(dw,dr.[Date])
       WHERE (sp.EndDate IS NULL OR dr.[Date] <= sp.EndDate) AND sp.StartDate <= dr.[Date]
	   AND (cv.TerminatedDate IS NULL OR dr.[Date] <= cv.TerminatedDate) AND cv.HiredDate <= dr.[Date]
UNION ALL
       SELECT
       iv.Type AS IntervalType
   	   ,CAST(CAST(iv.StartTime AS TIME) AS DATETIME) + CAST(CAST(dr.[Date] AS DATE)AS DATETIME) AS StartDateTime
   	   ,CAST(CAST(iv.EndTime AS TIME) AS DATETIME) + CAST(CAST(dr.[Date] AS DATE) AS DATETIME) AS EndDateTime
       ,cv.CarerID
	   ,cv.AomID
	   ,NULL AS OT
       FROM #DateRange dr
   	   CROSS JOIN SparkCare.dbo.CarerView cv WITH(NOLOCK)
	   INNER JOIN [People].dbo.EmployeePosition ep WITH(NOLOCK) ON ep.EmployeePositionID = cv.CarerID
       INNER JOIN [People].dbo.WorkSchedule ws WITH(NOLOCK)  ON ws.PositionID = ep.PositionID
       INNER JOIN [People].dbo.SchedulePattern sp WITH(NOLOCK) ON sp.SchedulePatternID = ws.SchedulePatternID
       INNER JOIN [People].dbo.WeekSlot wk WITH(NOLOCK) ON wk.SchedulePatternID = sp.SchedulePatternID AND wk.WeekNumber = People.dbo.GetActiveWeekForShiftPattern(sp.StartDate, dr.[Date], (SELECT COUNT(*) FROM [People].dbo.WeekSlot WHERE WeekSlot.SchedulePatternID = sp.SchedulePatternID), sp.StartingWeek)
       INNER JOIN [People].dbo.IntervalSlot iv WITH(NOLOCK) ON iv.WeekSlotId = wk.WeekSlotID AND iv.Day = DATEPART(dw,dr.[Date])
       WHERE (sp.EndDate IS NULL OR dr.[Date] <= sp.EndDate) AND sp.StartDate <= dr.[Date]
  	   AND (cv.TerminatedDate IS NULL OR dr.[Date] <= cv.TerminatedDate) AND cv.HiredDate <= dr.[Date]
       AND NOT EXISTS(SELECT sp.SchedulePatternID FROM [People].dbo.OverrideSchedule os WITH(NOLOCK)  INNER JOIN [People].dbo.SchedulePattern sp WITH(NOLOCK) ON sp.SchedulePatternID = os.SchedulePatternID WHERE os.EmployeePositionID = ep.EmployeePositionID AND (sp.EndDate IS NULL OR dr.[Date] <= sp.EndDate) AND sp.StartDate <= dr.[Date])
UNION 
   SELECT ov. IntervalType,  ---  include any agreed overtime intervals
          StartDateTime ,
          EndDateTime ,
          ov.CarerId,
		  cv.AomId  ,
		  1 AS OT      
   FROM SparkCare.dbo.OvertimeIntervalView ov
   INNER JOIN SparkCare.dbo.CarerView cv ON cv.CarerId = ov.CarerId
   INNER JOIN #DateRange dr ON dr.Date = ov.IntervalDate
) t1


--SELECT TOP 1000* FROM dbo.CarerIntervals

SELECT * FROM  SparkCare.dbo.OvertimeIntervalView

SELECT * FROM #CarerIntervals
 
ALTER TABLE #CarerIntervals ADD Leave VARCHAR(10), SickLeave   VARCHAR(10)

UPDATE #CarerIntervals  -- flag full leave days for carer...
SET leave = 'Day'
WHERE EXISTS(SELECT *--1
             FROM People.dbo.Leave l
			 WHERE l.EmployeePositionID = #CarerIntervals.CarerId
			 AND CAST(#CarerIntervals.StartDateTime AS DATE) BETWEEN CAST(l.StartDate AS DATE) AND CAST(ISNULL(l.EndDate,'20451231') AS DATE)
			 AND l.ApprovalStatus IN (1,2)
			 AND l.HalfDay = 0
			 )

UPDATE #CarerIntervals -- flag sick days for carer....
SET sickleave = 'Y'
WHERE EXISTS(SELECT *--1
             FROM People.dbo.SickLeave l
			 WHERE l.EmployeePositionID = #CarerIntervals.CarerId
			 AND CAST(#CarerIntervals.StartDateTime AS DATE) BETWEEN CAST(l.StartDate AS DATE) AND CAST(ISNULL(l.EndDate,'20451231') AS DATE)
			 AND l.Deleted = 0
			 )


ALTER TABLE #CarerIntervals ADD Overlap   BIT

-- Need to check if any carers have overlapping interval - this should only occur where an overtime interval clashes with a standard shift interval
-- In any cases where we find an overlap we will discard the standard shift interval and work on the assumption that the overtime interval is the valid one.
UPDATE #CarerIntervals
SET Overlap = 1
WHERE OT IS NULL
AND IntervalType = 1
AND EXISTS(SELECT 1
           FROM #CarerIntervals t2
		   WHERE t2.CarerId = #CarerIntervals.CarerId
		   AND t2.OT = 1
		   AND t2.StartDateTime < #CarerIntervals.EndDateTime
		   AND t2.EndDateTime > #CarerIntervals.StartDateTime
		   AND t2.IntervalType = 1
		   )



-- To get the final work schedule for carers.....

SELECT IntervalType ,
       StartDateTime ,
       EndDateTime ,
       CarerId ,
       AomId 
FROM #CarerIntervals
WHERE sickleave IS NULL 
AND (leave IS NULL OR OT = 1 ) -- carer can still choose to work overtime even if they are on leave
AND overlap IS NULL  -- exclude any conflicting intervals
AND IntervalType = 1 -- exclude breaks 
ORDER BY AomId, CarerId, StartDateTime

