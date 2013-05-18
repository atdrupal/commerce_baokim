<?php

/**
 * Menu callback: BPNs
 */
function commerce_baokim_bpn() {
  //get bpn data from params
  $ipn = $_POST;

  //load order
  $order = FALSE;
  if (!empty($ipn['order_id'])) {
    $order = commerce_order_load($ipn['order_id']);
  }

  if ($order === FALSE) {
    watchdog('commerce_baokim', t('Step1: FAILED - The order ID #@orderID is not exist.', array('@orderID' => $ipn['order_id'])));
    return FALSE;
  }

  //load payment method
  $payment_method = commerce_payment_method_instance_load($order->data['payment_method']);
  $settings = $payment_method['settings'];

  // include BaoKimPayment API
  module_load_include('inc', 'commerce_baokim', 'BaoKimPayment');
  $baoKim = new BaoKimPayment();

  //check verify payment with BaoKim
  $urlVerifyBK = ($settings['server'] == 'sandbox') ? BAOKIM_SANDBOX_VERIFY_URL : BAOKIM_LIVE_VERIFY_URL;
  $url = $baoKim->createVerifyPaymentURL($urlVerifyBK, $ipn);
  $verifyMessage = drupal_http_request($url, array('timeout' => 10));
  if ($verifyMessage == 'INVALID') {
    watchdog('commerce_baokim', t('Step1: FAILED - Payment of order #@orderid from @author (email: @email, phone: @phone,address: @address ) was failed.', array(
      '@orderid' => isset($ipn['order_id']) ? $ipn['order_id'] : '',
      '@author' => isset($ipn['customer_name']) ? $ipn['customer_name'] : '',
      '@email' => isset($ipn['customer_email']) ? $ipn['customer_email'] : '',
      '@phone' => isset($ipn['customer_phone']) ? $ipn['customer_phone'] : '',
      '@address' => isset($ipn['customer_address']) ? $ipn['customer_address'] : '')
      )
    );
    return FALSE;
  }

  //proccess order after validate success
  commerce_baokim_ipn_proccess($ipn, $order, $settings, $payment_method);
}

/**
 * Menu callback: baoKim success callback
 */
function commerce_baokim_success_callback() {
  global $base_url;

  //get bpn data from params
  $ipn = $_GET;

  //load order
  $order = FALSE;
  if (!empty($ipn['order_id'])) {
    $order = commerce_order_load($ipn['order_id']);
  }

  if ($order === FALSE) {
    watchdog('commerce_baokim', t('Step1: FAILED - The order ID #@orderID is not exist.', array('@orderID' => $ipn['order_id'])));
    drupal_set_message(t('Check out is failed because order id is not exist.'));
    drupal_goto($base_url);
  }

  // include BaoKimPayment API
  module_load_include('inc', 'commerce_baokim', 'BaoKimPayment');
  $baoKim = new BaoKimPayment();

  //unset q arguments before verify response URL
  unset($ipn['q']);

  if (!$baoKim->verifyResponseUrl($ipn)) {
    watchdog('commerce_baokim', t('Callback: FAILED - Payment of order #@orderid from @author (email: @email, phone: @phone,address: @address ) was failed.', array(
      '@orderid' => isset($ipn['order_id']) ? $ipn['order_id'] : '',
      '@author' => isset($ipn['customer_name']) ? $ipn['customer_name'] : '',
      '@email' => isset($ipn['customer_email']) ? $ipn['customer_email'] : '',
      '@phone' => isset($ipn['customer_phone']) ? $ipn['customer_phone'] : '',
      '@address' => isset($ipn['customer_address']) ? $ipn['customer_address'] : '')
      )
    );

    commerce_payment_redirect_pane_previous_page($order);
    drupal_set_message(t('Verify BaoKim data is not passed.'));
    drupal_goto($base_url);
  }

  //load payment method
  $payment_method = commerce_payment_method_instance_load($order->data['payment_method']);
  $settings = $payment_method['settings'];

  //proccess order after validate success
  return commerce_baokim_thank_you_page($ipn, $order, $settings, $payment_method);
}
