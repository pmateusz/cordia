# SQL Queries Manual

## Introduction

The purpose of the document is to introduce a collection of SQL queries that authors will execute on Cordia databases. Result sets produced by these queries will be then used to define instances of the Home Care Staff Rostering Problem (HCSRP). These instances will serve as example input to test methods developed for finding feasible staff and visit matchings that compose a schedule.

We would like to kindly ask the reader for a detailed review of the document and referenced SQL queries to verify that authors' understanding and interpretation of information available in Cordia databases is correct.

Apart from the confirmation that queries are correct and interpretation of data is sound we would be grateful for an assessment if we process enough information to find schedules which could be executed in practice. For example, if a method developer overlooks the fact that carers are to leave the method could not be used to compute valid schedules. (ANNALISA: not sure what you mean here)

## Problem Definition

In order to explain how results sets produced by queries will be used let us introduce our definition of the Home Care Staff Rostering Problem (HCSRP). The formulation presented below is compact, because we find a complete description is too complex for the purpose of this document. For the complete formulation of the problem refer to [the article](https://www.overleaf.com/11561165vkffcgvnkyzt#/44719205/).

Lets introduce the context of home care staff operations first:

* A set of home care workers should cover a set of visits,
* A visit is defined by its location, requested start time and a requested duration,
* Some visits should be covered by more than one worker,
* A carer is available for work within specified time periods each day known as a schedule pattern,
* There are exceptions when a carer is unavailable in that period due to a leave,
* A carer should not work more than a certain number of hours per week,
* Some carers can work overtime, which is bounded above as well,
* To ease company operations visits were partitioned by their spatial location into disjoint areas of operation and management (AOM),
* As a result of the reorganization each visit belongs to a single AOM,
* Furthermore, each carer works in a single AOM.

In practice making a visit consist of performing different activities by a worker at the visit location, although from our perspective and without loss of generality, visits are considered as spending certain amount of time by a worker at a particular location.

Lets define the HCSRP as finding an ordered set covering of visits where each subset of visits is assigned to a single carer. The carer is required to make all visits in the subset. Visits that require more than one carer will be effectively included in several subsets.

Due to the workforce and service demand decomposition to AOMs the HCSRP can be solved for each AOM independently.

In order to make the problem size more manageable two simplifications are made that do not impact the solution of the problem in a general case.

Firstly, we reduce solving the HCSRP to a single AOM. The solution for a general case can be obtained by merging solutions of sub-problems obtained for each AOM independently.

Secondly, since both visit occurrence and schedule patterns have regular, circular structure we limit the number of visits to the time window denoted as [`START_TIME`, `END_TIME`). The range boundaries correspond to the these of the `between` operator in SQL. The solution for a general case can be obtained by taking a time window large enough to include complete periods of visit occurrence and shift patterns.

## Queries

Having presented the home care scheduling problem domain and the adopted HCSRP formulation lets introduce the collection of queries. We will do it step by step gradually adding new information to an instance of the HCSRP.

Each subsection introduces a new query which can be run as is in a database session. For those who do not have such opportunity example result sets were made up. Any resemblance to actual events or locales or persons is entirely coincidental.

### Opt AOM

As of this writing there are 27 AOMs, which can be uniquely represented by managers. However, for the purpose of this section we will use an index `AOM_CODE` that takes values in the range `[1:27]`.

__[Query](https://github.com/pmateusz/cordia/blob/master/sql/people/list_aom.sql)__

__Results:__

|aom_code|aom_id|area_code|area_no|
|--------|------|---------|-------|
|1|111|A056|AB01|
|2|222|A477|AB07|
|26|888|A650|AB58|
|27|999|A899|AB60|

For the purpose of exposition lets assume that we are defining a HCSPR instance for the `AOM` 1 between `START_TIME` 2017-09-01 and `END_TIME` 2017-10-1.

### List visits

Get the list of visits in the AOM `1` that are requested between `2017-09-01` and `2017-10-1`.

__[Query](https://github.com/pmateusz/cordia/blob/master/sql/spark_care/list_visits_within_time_windows.sql)__

__Results:__

|visit_id|service_user_id|visit_date|requested_visit_time|requested_visit_duration|street|town|post_code|aom_code|
|--------|---------------|----------|--------------------|------------------------|------|----|---------|--------|
|1234567|2345678|2017-10-31|12:30:00|30|Blackfriars Road|Glasgow|G1 3J|1|

A visit description contains requested date, time, duration and address without house number. The query will be executed on an anonymized data-set with the last post code character trimmed. For the purpose of research the missing address information will be filled with random data.

The queries introduced so far return demand for service in a certain AOM within specified time range. Lets now move to workforce related queries.

### List home care workers

Get the list of employees who work on home carer positions in the AOM `1` that between `2017-09-01` and `2017-10-1`.

__[Query](https://github.com/pmateusz/cordia/blob/master/sql/people/list_employees_in_aom.sql)__

__Results:__

|employee_position_id|start_time|end_time|struct_aom|unit_aom|
|--------------------|----------|--------|----------|--------|
|1234567|2017-10-31|2017-11-30|1|1|

__Note:__ There are situations where a home carer belongs to different AOMs described in the data set as `AomOpStruct` and `AomOrgUnit`. How such a conflict should be resolved?

### List home carers' schedule patterns and workweek

Home carer availability is described using a schedule pattern. It is a collection of time slots mapped to classes of activity, such as `Shift` or `Break (Paid)`. A schedule pattern is valid for a certain period of time. Furthermore, a schedule be temporarily overridden.

Get the list of schedule patterns for employees who worked as home carers in AOM `1` between `2017-09-01` and `2017-10-1`.

__[Query](https://github.com/pmateusz/cordia/blob/master/sql/people/list_schedules.sql)__

__Results:__

|EmployeePositionId|SchedulePatternId|WorkWeek|Overtime|StartDate|EndDate|
|------------------|-----------------|--------|--------|---------|-------|
|123456|123|38|0|2017-10-31|2017-11-30|

The list contains employees, schedule patterns, the total number of regular working hours, overtime and date range when a schedule pattern is valid.

So far we found employees who work in the AOM `1` from `2017-09-01` to `2017-10-1` and their workweek commitments. We do not know though periods of time when they are available for work. This information can be extracted by the following query.

### List a schedule pattern details

Get the list of time slots that constitute a schedule pattern.

__[Query](https://github.com/pmateusz/cordia/blob/master/sql/people/list_schedule_details.sql)__

__Results:__

|SchedulePatternID|WeekNumber|Day|StartTime|EndTime|Type|
|-----------------|----------|---|---------|-------|----|
|123456|1|1|07:30:00|13:00:00|Shift|
|123456|1|1|13:00:00|16:30:00|Break (Paid)|
|123456|1|1|16:30:00|19:00:00|Shift|
|123456|1|1|19:00:00|20:00:00|Break (Paid)|
|123456|1|1|20:00:00|22:00:00|Shift|

Finally, there are exceptional situations when carers are not available for work. These include leaves and absences. The former are subject of the next subsection. The latter are unplanned, therefore outside the scope of our investigation.

### List leaves

Get the list of leaves requested by carers who work in the AOM `1` between `2017-09-01` and `2017-10-1`. A leave has to be formally requested by an employee to be then approved by a relevant manager. For the purpose of staff scheduling we are interested only in leaves that are likely to happen, therefore we filter out leave requests that were either cancelled or rejected.

__[Query](https://github.com/pmateusz/cordia/blob/master/sql/people/list_leaves.sql)__

__Results:__

|EmployeePositionId|StartDate|EndDate|HalfDay|HalfDayPeriod|ApprovalStatus|
|------------------|---------|-------|-------|-------------|--------------|
|123456|2017-09-12|2017-09-15|0|NULL|Approved|

## Conclusions

We introduced a collection of SQL queries that we are going to use to extract information required to define instances of the HCSRP. In authors understanding the presented collection is complete, by which we mean that there are no other queries whose results would have to be taken into consideration to define practical instances of the HCSRP.

If the queries pass the internal review, we are going to use them in development of a script that will automatically create an instance of HCSRP for given AOM and time range.
