<?php

/**
 * Cacllback for /commerce_baokim/callback/bpn
 */
function commerce_baokim_bpn() {
  // get bpn data from params
  $ipn = $_POST;

  // load order
  $order = FALSE;
  if (!empty($ipn['order_id'])) {
    $order = commerce_order_load($ipn['order_id']);
  }

  if ($order === FALSE) {
    watchdog('commerce_baokim', t('Step1: FAILED - The order ID #@orderID is not exist.', array('@orderID' => $ipn['order_id'])));
    return FALSE;
  }

  // load payment method
  $payment_method = commerce_payment_method_instance_load($order->data['payment_method']);
  $settings = $payment_method['settings'];

  // include BaoKimPayment API
  module_load_include('inc', 'commerce_baokim', 'BaoKimPayment');
  $baoKim = new BaoKimPayment();

  // check verify payment with BaoKim
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

  // proccess order after validate success
  commerce_baokim_ipn_proccess($ipn, $order, $settings, $payment_method);
}

/**
 *
 * @param type $transaction_status
 * @return type
 */
function commerce_baokim_check_transaction_status($transaction_status) {
  //check and store payment info
  $msg = commerce_baokim_get_payment_status_name($transaction_status);
  $status = 'unknown';

  switch ($transaction_status) {
    // payment pending
    case 1:
    case 2:
    case 13:
      $status = COMMERCE_PAYMENT_STATUS_PENDING;
      break;

    // payment failed
    case 5:
    case 6:
    case 7:
    case 8:
    case 12:
    case 15:
      $status = COMMERCE_PAYMENT_STATUS_FAILURE;
      break;

    // payment completed
    case 4:
      //check if ipn total_amount != order's total_amount
      //do not change transaction_status to COMPLETED
      //need it?

      $status = COMMERCE_PAYMENT_STATUS_SUCCESS;
      $msg = t('The payment has completed.');
      break;

    //other status
    default:
      $status = COMMERCE_PAYMENT_STATUS_FAILURE;
      $msg = t('Do not know what\'s happended. Payment status (transaction_status from BaoKim) @id', array('@id' => $transaction_status));
      break;
  }

  return array(
    'message' => $msg,
    'status' => $status
  );
}

/**
 * Function proccess BPN callback
 */
function commerce_baokim_ipn_proccess($ipn, $order, $settings, $payment_method) {
  //verify success
  watchdog('commerce_baokim', 'Step1: SUCCESS - Payment response is verified.');

  // If this is a prior authorization capture IPN for which we've already
  // created a transaction...
  $auth_ipn = commerce_baokim_ipn_load($ipn['transaction_id']);
  if ($auth_ipn) {
    // Load the prior IPN's transaction and update that with the capture values.
    $transaction = commerce_payment_transaction_load($auth_ipn['transaction_id']);
  }
  else {
    // Create a new payment transaction for the order.
    $transaction = commerce_payment_transaction_new('baokim', $order->order_id);
    $transaction->instance_id = $payment_method['instance_id'];
  }
  $transaction->remote_id = $ipn['transaction_id'];

  // BaoKim only supports VND
  $transaction->amount = commerce_currency_decimal_to_amount($ipn['total_amount'], $settings['currency_code']);
  $transaction->currency_code = $settings['currency_code'];
  $transaction->payload[REQUEST_TIME] = $ipn;
  $transaction->payment_method = 'baokim';

  // Set the transaction's statuses based on the IPN's payment_status.
  $transaction->remote_status = $ipn['transaction_status'];

  // check and store payment info
  $rs = commerce_baokim_check_transaction_status($ipn['transaction_status']);
  $transaction->message = $rs['message'];
  $transaction->status = $rs['status'];

  // ensure data before write a record
  $ipn['txn_id'] = $ipn['transaction_id'];
  $ipn['payment_status'] = $ipn['transaction_status'];

  // save payment info and commerce baokim ipn info
  // save payment info
  // Save the transaction information.
  commerce_payment_transaction_save($transaction);
  $ipn['transaction_id'] = $transaction->transaction_id;

  //save BaoKim ipn
  commerce_baokim_ipn_save($ipn);

  // Create a billing profile based on the IPN if enabled.
  commerce_payment_redirect_pane_next_page($order);
  watchdog('commerce_baokim', 'Step2: IPN processed for Order @order_number with ID @txn_id.', array('@txn_id' => $ipn['txn_id'], '@order_number' => $order->order_number), WATCHDOG_INFO);
}

/**
 * Callback for /commerce_baokim/callback.
 * 
 */
function commerce_baokim_success_callback() {
  global $base_url;

  // get bpn data from params
  $ipn = $_GET;

  // load order
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

  // unset q arguments before verify response URL
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

  // load payment method
  $payment_method = commerce_payment_method_instance_load($order->data['payment_method']);
  $settings = $payment_method['settings'];

  // proccess order after validate success
  return commerce_baokim_thank_you_page($ipn, $order, $settings, $payment_method);
}

/**
 * Say thank you page
 */
function commerce_baokim_thank_you_page($ipn, $order, $settings, $payment_method) {
  $output = t('Your order is number @order_id. You can view your order on your account page when logged in.', array('@order_id' => $order->order_id));
  $output .= '<br/>' . t('Thank you for your payment.');
  return $output;
}
