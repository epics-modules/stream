# stream
APS BCDA synApps module: stream

For more information, see
   http://www.aps.anl.gov/bcda/synApps

converted from APS SVN repository: Fri Nov 20 17:54:00 CST 2015

Regarding the license of tagged versions prior to synApps 4-5,
refer to http://www.aps.anl.gov/bcda/synApps/license.php

The synApps stream module is a wrapper around
the StreamDevice support from PSI.
Refer to  https://github.com/paulscherrerinstitute/StreamDevice
to obtain the StreamDevice source.

When cloning this repository, you will have to run

    git submodule init      
and then

    git submodule update  
in the StreamDevice folder in order to sucessfully pull
the code from the Streams PSI repository.

**NOTE**: Please do not post issues or pull requests for 
StreamDevice in this GitHub repository. Use the PSI github repo
instead.
