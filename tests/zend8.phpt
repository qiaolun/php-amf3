--TEST--
ZendAMF compatibility test 8
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

$data = unserialize('a:1:{i:960001;O:8:"stdClass":1:{s:2:"45";i:0;}}');

$out = amf3_encode($data);
echo bin2hex($out);


--EXPECT--
0903010a0b01053435040001
