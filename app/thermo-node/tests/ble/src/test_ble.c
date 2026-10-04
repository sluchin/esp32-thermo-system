/**
 * @file test_ble.c
 * @brief ble.c の単体テスト
 *
 * Bluetooth スタックの関数 (bt_enable, bt_le_adv_start) を, FFF のモックに置き換えて,
 * ble_init() と ble_advertise() が, 正しい引数で呼び出し, エラーを返すことを確認する.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ble.h"

DEFINE_FFF_GLOBALS

/** 期待するデバイス名 (CMakeLists.txt で定義) */
#define EXPECTED_NAME "Thermo-Node"
/** アドバタイズデータの数 (フラグ, 名前) */
#define AD_COUNT      2u

FAKE_VALUE_FUNC(int, bt_enable, bt_ready_cb_t)
FAKE_VALUE_FUNC(int, bt_le_adv_start, const struct bt_le_adv_param *,
		const struct bt_data *, size_t, const struct bt_data *, size_t)

/** bt_le_adv_start() の引数 (呼び出しの後は, 引数の指す先が無効になるので, 写しを残す) */
static struct {
	struct bt_le_adv_param param;       /**< アドバタイズパラメータ */
	struct bt_data ad[AD_COUNT];        /**< アドバタイズデータ */
	uint8_t name[sizeof(EXPECTED_NAME)]; /**< 名前の写し (終端付き) */
	uint8_t flags;                      /**< フラグの写し */
	size_t ad_len;                      /**< アドバタイズデータの数 */
	const struct bt_data *sd;           /**< スキャン応答データ */
	size_t sd_len;                      /**< スキャン応答データの数 */
} captured;

/**
 * bt_le_adv_start() のモック動作 (引数の写しを残す)
 *
 * @param[in] param アドバタイズパラメータ
 * @param[in] ad アドバタイズデータ
 * @param[in] ad_len アドバタイズデータの数
 * @param[in] sd スキャン応答データ
 * @param[in] sd_len スキャン応答データの数
 * @return 0
 */
static int capture_adv_start(const struct bt_le_adv_param *param,
			     const struct bt_data *ad, size_t ad_len,
			     const struct bt_data *sd, size_t sd_len)
{
	size_t i = 0u;

	(void)memset(&captured, 0, sizeof(captured));
	captured.param = *param;
	captured.ad_len = ad_len;
	captured.sd = sd;
	captured.sd_len = sd_len;
	for (i = 0u; (i < ad_len) && (i < AD_COUNT); i++) {
		captured.ad[i] = ad[i];
	}
	if ((ad_len >= 1u) && (ad[0].data_len == 1u)) {
		captured.flags = ad[0].data[0];
	}
	if ((ad_len >= 2u) && (ad[1].data_len < sizeof(captured.name))) {
		(void)memcpy(captured.name, ad[1].data, ad[1].data_len);
	}
	return 0;
}

/**
 * 各テストの前に, モックを初期状態に戻す
 *
 * @param[in] fixture 使用しない
 */
static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(bt_enable);
	RESET_FAKE(bt_le_adv_start);
	FFF_RESET_HISTORY();
	(void)memset(&captured, 0, sizeof(captured));
}

/** ble_init() は, bt_enable(NULL) を 1 回呼んで, 成功する */
ZTEST(ble_node, test_init_success)
{
	bt_enable_fake.return_val = 0;

	zassert_equal(ble_init(), EXIT_SUCCESS);
	zassert_equal(bt_enable_fake.call_count, 1u);
	zassert_is_null(bt_enable_fake.arg0_val);
}

/** ble_init() は, bt_enable() のエラーを, そのまま返す */
ZTEST(ble_node, test_init_failure)
{
	bt_enable_fake.return_val = -EIO;

	zassert_equal(ble_init(), -EIO);
	zassert_equal(bt_enable_fake.call_count, 1u);
}

/** ble_advertise() は, 接続可能なアドバタイズを, フラグと名前のデータ付きで始める */
ZTEST(ble_node, test_advertise_success)
{
	bt_le_adv_start_fake.custom_fake = capture_adv_start;

	zassert_equal(ble_advertise(), EXIT_SUCCESS);
	zassert_equal(bt_le_adv_start_fake.call_count, 1u);

	/* 接続可能 (BT_LE_ADV_CONN_FAST_1) */
	zassert_true((captured.param.options & BT_LE_ADV_OPT_CONN) != 0u);
	zassert_equal(captured.param.interval_min, BT_GAP_ADV_FAST_INT_MIN_1);
	zassert_equal(captured.param.interval_max, BT_GAP_ADV_FAST_INT_MAX_1);

	/* アドバタイズデータ: フラグ, 完全なデバイス名 */
	zassert_equal(captured.ad_len, AD_COUNT);
	zassert_equal(captured.ad[0].type, BT_DATA_FLAGS);
	zassert_equal(captured.flags, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR));
	zassert_equal(captured.ad[1].type, BT_DATA_NAME_COMPLETE);
	zassert_equal(captured.ad[1].data_len, strlen(EXPECTED_NAME));
	zassert_mem_equal(captured.name, EXPECTED_NAME, strlen(EXPECTED_NAME));

	/* スキャン応答データはない */
	zassert_is_null(captured.sd);
	zassert_equal(captured.sd_len, 0u);
}

/** ble_advertise() は, bt_le_adv_start() のエラーを, そのまま返す */
ZTEST(ble_node, test_advertise_failure)
{
	bt_le_adv_start_fake.return_val = -ENOMEM;

	zassert_equal(ble_advertise(), -ENOMEM);
	zassert_equal(bt_le_adv_start_fake.call_count, 1u);
}

ZTEST_SUITE(ble_node, NULL, NULL, before, NULL, NULL);
