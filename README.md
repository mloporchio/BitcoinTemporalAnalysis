# Temporal analysis of Bitcoin graphs

This repository contains the code used to perform a temporal analysis of the Bitcoin Payment Graph.

The study constructs **28 cumulative Payment Graphs** covering the time period from **1 January 2009 to 31 December 2022**. Each graph starts at the same initial timestamp (`2009-01-01 00:00:00 UTC`) and incorporates Bitcoin transactions up to a different six-month observation point. Thus, the graphs form a sequence of increasingly complete snapshots of the Bitcoin transaction history.

The current analysis focuses on the temporal evolution of the graph topology, including:

* **In-degree distributions**
* **Out-degree distributions**
* **Weakly connected component (WCC) size distributions**
* **Statistical comparison of degree distributions across different temporal snapshots**

The 28 observation points are listed below.

| chunk_id | start_date              | end_date                |
| -------: | ----------------------- | ----------------------- |
|        1 | 2009-01-01 00:00:00 UTC | 2009-06-30 23:59:59 UTC |
|        2 | 2009-01-01 00:00:00 UTC | 2009-12-31 23:59:59 UTC |
|        3 | 2009-01-01 00:00:00 UTC | 2010-06-30 23:59:59 UTC |
|        4 | 2009-01-01 00:00:00 UTC | 2010-12-31 23:59:59 UTC |
|        5 | 2009-01-01 00:00:00 UTC | 2011-06-30 23:59:59 UTC |
|        6 | 2009-01-01 00:00:00 UTC | 2011-12-31 23:59:59 UTC |
|        7 | 2009-01-01 00:00:00 UTC | 2012-06-30 23:59:59 UTC |
|        8 | 2009-01-01 00:00:00 UTC | 2012-12-31 23:59:59 UTC |
|        9 | 2009-01-01 00:00:00 UTC | 2013-06-30 23:59:59 UTC |
|       10 | 2009-01-01 00:00:00 UTC | 2013-12-31 23:59:59 UTC |
|       11 | 2009-01-01 00:00:00 UTC | 2014-06-30 23:59:59 UTC |
|       12 | 2009-01-01 00:00:00 UTC | 2014-12-31 23:59:59 UTC |
|       13 | 2009-01-01 00:00:00 UTC | 2015-06-30 23:59:59 UTC |
|       14 | 2009-01-01 00:00:00 UTC | 2015-12-31 23:59:59 UTC |
|       15 | 2009-01-01 00:00:00 UTC | 2016-06-30 23:59:59 UTC |
|       16 | 2009-01-01 00:00:00 UTC | 2016-12-31 23:59:59 UTC |
|       17 | 2009-01-01 00:00:00 UTC | 2017-06-30 23:59:59 UTC |
|       18 | 2009-01-01 00:00:00 UTC | 2017-12-31 23:59:59 UTC |
|       19 | 2009-01-01 00:00:00 UTC | 2018-06-30 23:59:59 UTC |
|       20 | 2009-01-01 00:00:00 UTC | 2018-12-31 23:59:59 UTC |
|       21 | 2009-01-01 00:00:00 UTC | 2019-06-30 23:59:59 UTC |
|       22 | 2009-01-01 00:00:00 UTC | 2019-12-31 23:59:59 UTC |
|       23 | 2009-01-01 00:00:00 UTC | 2020-06-30 23:59:59 UTC |
|       24 | 2009-01-01 00:00:00 UTC | 2020-12-31 23:59:59 UTC |
|       25 | 2009-01-01 00:00:00 UTC | 2021-06-30 23:59:59 UTC |
|       26 | 2009-01-01 00:00:00 UTC | 2021-12-31 23:59:59 UTC |
|       27 | 2009-01-01 00:00:00 UTC | 2022-06-30 23:59:59 UTC |
|       28 | 2009-01-01 00:00:00 UTC | 2022-12-31 23:59:59 UTC |
