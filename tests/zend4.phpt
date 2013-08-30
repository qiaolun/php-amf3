--TEST--
ZendAMF compatibility test 4
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

$data = array(
    1 => 'aa',
    2 => 'bb',
    'x' => 'cc',
);

$out = amf3_encode($data);
echo bin2hex($out);


--EXPECT--
0905037806056363010605616106056262
