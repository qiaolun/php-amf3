--TEST--
ZendAMF compatibility test 5
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

$data = array(
    'x' => 'aa',
    'y' => 'bb',
    'z' => 'cc',
);

$out = amf3_encode($data);
echo bin2hex($out);


--EXPECT--
0901037806056161037906056262037a0605636301
