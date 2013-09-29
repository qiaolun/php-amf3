--TEST--
ZendAMF compatibility test 1, big int
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

$data = 2222222222;
$out = amf3_encode($data);
echo bin2hex($out);


--EXPECT--
0541e08e8d71c00000
