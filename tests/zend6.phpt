--TEST--
ZendAMF compatibility test 6
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

$data = (object)array(
    'x' => 'aa',
    'y' => 'bb',
    'z' => 'cc',
);

$out = amf3_encode($data);
echo bin2hex($out);


--EXPECT--
0a0b01037806056161037906056262037a0605636301
