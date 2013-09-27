--TEST--
ZendAMF compatibility test class 3, empty object
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php


$data = (object)array(
    'a' => (object)array(),
    'b' => (object)array(),
    'c' => (object)array(),
);

$out = amf3_encode($data);
echo bin2hex($out);


--EXPECT--
0a0b0103610a010103620a010103630a010101
