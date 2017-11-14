# Introduction

The purpose of the document is to describe a collection of SQL queries that will executed on databases managed by Cordia. The result sets produced by these queries will be
used to define instances of the Home Care Staff Rostering Problem. The instances will be then used in develpment of methods for computing feasible staff rosterings.

We would like to kindly ask the reader for a review of the document and confirmation that understaning and interpretation of information provided in the dataset by authors is correct.

# Problem Definition

In order to explain how information obtained by executing queries will be used and interpreted let us introduce a simplified definition of the Home Care Staff Rostering Problem. For the complete description of the model, refer to [the article]().

A set of home care workers `W` should cover a set of visits `V`. A visit `v`, without loss of generality, is modelled as spending some amont of time by a worker in a particular location. Some visits should be covered by more than one worker. A visit is defined by its location, requested start time and a requested duration.

Each worker is available for work within specified time periods each day known as schedule. There are exceptions when a carer is unavailable in that period due to leave.

To ease the operation and management visits were partitioned into areas of operation and management (AOM) depending on their location. Each visit belongs to a single AOM.
Furthermore, each worker makes visits within a single AOM.

Lets define the Home Care Staff Rostering Problem finidng an ordered set covering of visits where each subset of visits is then assigned to a single worker who is eligible to make all visits in the subset.
Visits that require more than one woker will be effectively included in several subsets.

Due to a decomposition of workforce and demand for service the Home Care Staff Rostering Problem can be solved for each AOM indepently.

In order to make the problem size more manageable make two simplyfing assumptions that do not impact the solution of the problem in a general case.

Firstly, we focus on solving the Home Care Staff Rostering Problem within a single AOM. The solution in a general case can be obtained by merging solutions obtained by solving subproblems for each AOM independelntly.

Secondly, since both visits occurence and schedules have regular, circular patterns we limit the size of the problem to a time window denoted as [`START_TIME`, `END_TIME`). It corresponds to the range boundaries used by the `between` operator defined for the date type in SQL. Solution of the general case can be obtained by taking a time window equal that includes full periods of visits and shift patterns.

# Queries
The following requsts sets were fabricated for the purpose of exposition. Run the attached queries on the dataset to obtain real results.

## List areas of operation and management
Return the list of available `AOM_CODE` values for reference in the future queries.

### [Query](https://github.com/pmateusz/cordia/blob/master/sql/people/list_aom.sql)

### Results
|aom_code|aom_id|area_code|area_no|
|--------|------|---------|-------|
|1|111|A056|AB01|
|2|222|A477|AB07|
|26|888|A650|AB58|
|27|999|A899|AB60|

## List home carers

### [Query](https://github.com/pmateusz/cordia/blob/master/sql/people/list_employees_in_aom.sql)
Return the list of employees who worked on a carer position between `START_TIME` and `END_TIME` in the specified AOM.

|Parameter|Value|
|---------|-----|
|`START_TIME`|`2017-10-31`|
|`END_TIME`|`2017-11-30`|
|`AOM_CODE`|1|

### Results
|employee_position_id|start_time|end_time|struct_aom|unit_aom|
|--------------------|----------|--------|----------|--------|
|1234567|2017-10-31|2017-11-30|1|1|

:trollface: It may happen that a home carer belongs to different AOMs recognized in the data set as `AomOpStruct` and `AomOrgUnit`. How such a conflict should be resolved?

## List home carers' schedules and workweek
Return the list of schedules of employees who worked on a carer positions between `START_TIME` and `END_TIME` in the specified AOM.

|Parameter|Value|
|---------|-----|
|`START_TIME`|`2017-10-31`|
|`END_TIME`|`2017-11-30`|
|`AOM_CODE`|1|

### [Query](https://github.com/pmateusz/cordia/blob/master/sql/people/list_schedules.sql)

### Results
|EmployeePositionId|SchedulePatternId|WorkWeek|Overtime|StartDate|EndDate|
|------------------|-----------------|--------|--------|---------|-------|
|123456|123|38|0|2017-10-31|2017-11-30|

The list of schedules contains the number of regular working hours of an employee within a week, overtime and dates when a schedule is valid.

## List schedule details
Return the list of time slots that constitute a schedule.

### [Query](https://github.com/pmateusz/cordia/blob/master/sql/people/list_schedule_details.sql)

|Parameter|Value|
|---------|-----|
|`SCHEDULE_PATTERN_ID`|`123456`|

#### Results
|SchedulePatternID|WeekNumber|Day|StartTime|EndTime|Type|
|-----------------|----------|---|---------|-------|----|
|123456|1|1|07:30:00.0000000|13:00:00.0000000|Shift|
|123456|1|1|13:00:00.0000000|16:30:00.0000000|Break (Paid)|
|123456|1|1|16:30:00.0000000|19:00:00.0000000|Shift|
|123456|1|1|19:00:00.0000000|20:00:00.0000000|Break (Paid)|
|123456|1|1|20:00:00.0000000|22:00:00.0000000|Shift|

## List leaves
Return the list of approved leaves between `START_TIME` and `END_TIME` of carers who work in the specified AOM.

|Parameter|Value|
|---------|-----|
|`START_TIME`|`2017-10-31`|
|`END_TIME`|`2017-11-30`|
|`AOM_CODE`|1|

### [Query](https://github.com/pmateusz/cordia/blob/master/sql/people/list_leaves.sql)

#### Results
|EmployeePositionId|StartDate|EndDate|HalfDay|HalfDayPeriod|ApprovalStatus|
|------------------|---------|-------|-------|-------------|--------------|
|123456|2017-09-12|2017-09-15|0|NULL|Approved|

## List visits
Return the list of visits that were requested to be performed within the specified time windows within a single aom.

### [Query](https://github.com/pmateusz/cordia/blob/master/sql/spark_care/list_visits_within_time_windows.sql)

|Parameter|Value|
|---------|-----|
|`START_TIME`|`2017-10-31`|
|`END_TIME`|`2017-11-30`|
|`AOM_CODE`|1|

### Results

|visit_id|service_user_id|visit_date|requested_visit_time|requested_visit_duration|street|town|post_code|aom_code|
|--------|---------------|----------|--------------------|------------------------|------|----|---------|--------|
|1234567|2345678|2017-10-31|12:30:00|30|Blackfriars Road|Glasgow|G1 3JW|1|

The list of visits contains a destination address, a requested start time and a requested duration defined for each item.
