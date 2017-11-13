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
[Source](https://github.com/pmateusz/cordia/blob/master/sql/people/list_aom.sql)


|aom_code|aom_id|area_code|area_no|
|--------|------|---------|-------|
|1|111|A056|AB01|
|2|222|A477|AB07|
|26|888|A650|AB58|
|27|999|A899|AB60|


## List of home carers working in a single aom

## List of vists within a single aom
[Source](https://github.com/pmateusz/cordia/blob/master/sql/spark_care/list_visits_within_time_windows.sql)

|visit_id|service_user_id|visit_date|requested_visit_time|requested_visit_duration|street|town|post_code|aom_code|
|--------|---------------|----------|--------------------|------------------------|------|----|---------|--------|
|1234567|2345678|2017-10-31|12:30:00|30|Blackfriars Road|Glasgow|G1 3JW|1|
