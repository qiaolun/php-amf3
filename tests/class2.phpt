--TEST--
ZendAMF compatibility test class 2, empty object
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

class Config_test extends ArrayObject {
}
$data = new Config_Test();

$out = amf3_encode($data);
echo bin2hex($out);


--EXPECT--
0a0317436f6e6669675f74657374
