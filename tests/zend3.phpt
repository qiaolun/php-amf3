--TEST--
ZendAMF compatibility test 3, numeric keys
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

$data = array(
    1 => 'aa',
    10 => 'bb',
    100 => 'cc',
);

$out = amf3_encode($data);
echo bin2hex($out);


--EXPECT--
090701060561610605626206056363
