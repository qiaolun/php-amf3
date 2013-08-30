--TEST--
ZendAMF compatibility test 7
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

$data = (object)array(
    'a' => null,
    'b' => 12345,
    'c' => 12345678901234567890,
    'd' => array(),
    'e' => (new stdClass()),
    'f' => (object)array(),
    'g' => (object)array(
        'x' => 1,
        'y' => null,
        'z' => '123',
    ),
);

$out = amf3_encode($data);
echo bin2hex($out);


--EXPECT--
0a0b01036101036204e03903630543e56a95319d63e1036409010103650a010103660a010103670a0103780401037901037a06073132330101
