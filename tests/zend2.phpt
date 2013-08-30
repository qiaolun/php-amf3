--TEST--
ZendAMF compatibility test 2, numeric keys start from zero
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

$data = array(
    0 => 'aa',
    1 => 'bb',
    2 => 'cc',
);
$out2 = amf3_encode($data);
echo bin2hex($out2);


--EXPECT--
090701060561610605626206056363
