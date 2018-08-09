=====================
DiskPrediction plugin
=====================

Disk prediction plugin provide two mode: The cloud mode is used to collect disk information and 
ceph status from Ceph cluster and send these data to Disk prediction server. The Disk prediction server 
related on the these data and provide the device health prediction state through by the AI engine predicted.
The local mode use plugin internal predicted model to do the simple device health prediction that related on 
the device smart data. The plugin use this physical devices healthy prediction data and use the Ceph device 
command to write the life expectancy date.


Enabling
========

Run the following command to enable *diskprediction* module in the Ceph
environment:

::

    ceph mgr module enable diskprediction


Select the plugin use mode:

::

    ceph diskprediction config-mode <local/cloud>


Local Mode
----------

The ceph diskprediction plugin use internal predictor module to do the device health prediction. It related on 
the device health plugin collected health metrics. The local predictor model predict the device health state 
requires at least six days of the device health metrics.


Cloud Mode Connection settings
------------------------------

The user need register on the link http://federator-ai-homepage.s3-website-us-west-2.amazonaws.com/#/.
The registration process will provide the disk prediction plugin used server and account information.
Run the following command to set up connection with the registration information between Ceph system 
and DiskPrediction server.

::

    ceph diskprediction config-set <diskprediction_server> <diskprediction_user> <diskprediction_password>
	

The ``<diskprediction_server>`` parameter is DiskPrediction server name, and it
could be an IP address if required.

The ``<diskprediction_user>`` and ``<diskprediction_password>`` parameters are the user
id and password logging in to the DiskPrediction server.



The connection settings can be shown using the following command:

::

    ceph diskprediction config-show


Addition optional configuration settings are:

:diskprediction_upload_metrics_interval: Time between reports ceph metrics to the diskprediction server.  Default 10 minutes.
:diskprediction_upload_smart_interval: Time between reports ceph physical device info to the diskprediction server.  Default is 12 hours.
:diskprediction_retrieve_prediction_interval: Time between fetch physical device health prediction data from the server.  Default is 12 hours.



Actively agents
===============

The plugin actively agents send/retrieve information with a Disk prediction server like:


Metrics agent
-------------
- Ceph cluster status
- Ceph mon/osd performance counts
- Ceph pool statistics
- Ceph each objects correlation information
- The plugin agent information
- The plugin agent cluster information
- The plugin agent host information
- Ceph physical device metadata


Smart agent
-----------
- Ceph physical device smart data (by smartctl command)


Prediction agent
----------------
- Retrieve the ceph physical device prediction data
 

Receiving predicted health status from a Ceph OSD disk drive
============================================================

You can receive predicted health status from Ceph OSD disk drive by using the
following command.

::

    ceph diskprediction get-predicted-status <device id>


get-predicted-status response
-----------------------------

::

    {
        "<device id>": {
            "prediction": {
            "sdb": {
                "near_failure": "Good",
                "disk_wwn": "5000011111111111",
                "serial_number": "111111111",
                "predicted": "2018-05-30 18:33:12",
                "device": "sdb"
                }
            }
        }
    }


+--------------------+-----------------------------------------------------+
|Attribute           | Description                                         |
+====================+=====================================================+
|near_failure        | The disk failure prediction state:                  |
|                    | Good/Warning/Bad/Unknown                            |
+--------------------+-----------------------------------------------------+
|disk_wwn            | Disk WWN number                                     |
+--------------------+-----------------------------------------------------+
|serial_number       | Disk serial number                                  |
+--------------------+-----------------------------------------------------+
|predicted           | Predicted date                                      |
+--------------------+-----------------------------------------------------+
|device              | device name on the local system                     |
+--------------------+-----------------------------------------------------+

The plugin reference the prediction near_failure state to wite the ceph devcie life expectancy days.

+--------------------+-----------------------------------------------------+
|near_failure        | Life expectancy days                                |
+====================+=====================================================+
|Good                | > 6 weeks                                           |
+--------------------+-----------------------------------------------------+
|Warning             | 2 weeks ~ 6 weeks                                   |
+--------------------+-----------------------------------------------------+
|Bad                 | < 2 weeks                                           |
+--------------------+-----------------------------------------------------+


Debugging
---------

If you want to debug the DiskPrediction module mapping to Ceph logging level,
use the following command.

::

    [mgr]

        debug mgr = 20

With logging set to debug for the manager the plugin will print out logging
message with prefix *mgr[diskprediction]* for easy filtering.

