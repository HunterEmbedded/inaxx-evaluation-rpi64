<?php
	# first remove file that manages stop of capture
	if(file_exists("stopCapture"))
		unlink("stopCapture");
	
	# and then call executable to do measurement
        #system("/opt/capture-current-iio &");
	exec(sprintf("%s > %s 2>&1 & echo $! >> %s","/opt/capture-current-iio","code-output.txt","file.pid"));

?>

