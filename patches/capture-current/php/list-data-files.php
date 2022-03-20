<?php
	# list the files
        foreach (glob("sql/current*") as $filename) {
            if (!empty($filename)){
		echo "$filename\n";
		 }
	}

?>

