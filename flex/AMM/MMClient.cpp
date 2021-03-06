#include "MMClient.hpp"
#include "TradingPair.hpp"

#include <tvm/contract.hpp>
#include <tvm/smart_switcher.hpp>
#include <tvm/contract_handle.hpp>
#include <tvm/replay_attack_protection/timestamp.hpp>
#include <tvm/default_support_functions.hpp>

using namespace tvm;
using namespace schema;

static constexpr unsigned TIMESTAMP_DELAY = 1800;

class FLeXClient final : public smart_interface<IFLeXClient>, public DFLeXClient {
  using replay_protection_t = replay_attack_protection::timestamp<TIMESTAMP_DELAY>;
public:
  struct error_code : tvm::error_code {
    static constexpr unsigned message_sender_is_not_my_owner = 100;
  };

  __always_inline
  void constructor(uint256 owner, 
  cell trading_pair_code,   
  uint128 transfer_tip3,
  uint128 return_ownership,
  uint128 trading_pair_deploy,
  uint128 order_answer,
  uint128 process_queue,
  uint128 send_notify, 
  address flex, 
  address notify_addr) {
    tons_cfg_.transfer_tip3 = transfer_tip3;
    tons_cfg_.return_ownership = return_ownership;
    tons_cfg_.trading_pair_deploy = trading_pair_deploy;
    tons_cfg_.order_answer = order_answer;
    tons_cfg_.process_queue = process_queue;
    tons_cfg_.send_notify = send_notify;    
    trading_pair_code_ = trading_pair_code;
    owner_ = owner;
    workchain_id_ = std::get<addr_std>(address{tvm_myaddr()}.val()).workchain_id;
    flex_ = flex;
    notify_addr_ = notify_addr;
  }

  __always_inline
  address deployTradingPair(address flex_addr, address tip3_root,
                            uint128 deploy_min_value, uint128 deploy_value) {
    DTradingPair pair_data {
      .flex_addr_ = flex_addr,
      .tip3_root_ = tip3_root,
      .deploy_value_ = deploy_min_value
    };

    auto [state_init, std_addr] = prepare_trading_pair_state_init_and_addr(pair_data, trading_pair_code_);
    auto trade_pair = ITradingPairPtr(address::make_std(workchain_id_, std_addr));
    trade_pair.deploy(state_init, Grams(deploy_value.get()), DEFAULT_MSG_FLAGS, false).onDeploy();

    return trade_pair.get();
  }

  __always_inline
  address deployPriceWithSell(cell args_cl) {
    tvm_accept();

    auto args = parse<FLeXSellArgs>(args_cl.ctos());

    auto [state_init, addr, std_addr] =
      preparePrice(args.price, args.min_amount, args.deals_limit, args.tip3cfg(), args.price_code);
    auto price_addr = IPricePtr(addr);
    cell deploy_init_cl = build(state_init).endc();
    SellArgs sell_args = {
      .amount = args.amount,
      .receive_wallet = args.addrs().receive_wallet
    };
    cell payload = build(sell_args).endc();

    ITONTokenWalletPtr my_tip3(args.addrs().my_tip3_addr);
    my_tip3(Grams(args.tons_value.get()), DEFAULT_MSG_FLAGS, false).
      lendOwnership(std_addr, args.amount, args.lend_finish_time, deploy_init_cl, payload);

    return price_addr.get();
  }

  __always_inline
  address deployPriceWithBuy(cell args_cl) {
    tvm_accept();
  
    auto args = parse<FLeXBuyArgs>(args_cl.ctos());
    auto [state_init, addr, std_addr] =
      preparePrice(args.price, args.min_amount, args.deals_limit, args.tip3cfg(), args.price_code);
    IPricePtr price_addr(addr);
    price_addr.deploy(state_init, Grams(args.deploy_value.get()), DEFAULT_MSG_FLAGS, false).
      buyTip3(args.amount, args.my_tip3_addr, args.order_finish_time);
    return price_addr.get();
  }
  __always_inline
  void cancelSellOrder(cell args_cl) {
    tvm_accept();

    auto args = parse<FLeXCancelArgs>(args_cl.ctos());
    auto [state_init, addr, std_addr] =
      preparePrice(args.price, args.min_amount, args.deals_limit, args.tip3cfg(), args.price_code);
    IPricePtr price_addr(addr);
    price_addr(Grams(args.value.get()), DEFAULT_MSG_FLAGS, false).cancelSell();
  }

  __always_inline
  void cancelBuyOrder(cell args_cl) {
    tvm_accept();

    auto args = parse<FLeXCancelArgs>(args_cl.ctos());
    auto [state_init, addr, std_addr] =
      preparePrice(args.price, args.min_amount, args.deals_limit, args.tip3cfg(), args.price_code);
    IPricePtr price_addr(addr);
    price_addr(Grams(args.value.get()), DEFAULT_MSG_FLAGS, false).cancelBuy();
  }

  __always_inline
  void transfer(address dest, uint128 value, bool_t bounce) {
    tvm_transfer(dest, value.get(), bounce.get());
  }

  __always_inline
  uint256 getOwner() {
    return owner_;
  }

  __always_inline
  address getFLeX() {
    return flex_;
  }

  // =============== Support functions ==================
  DEFAULT_SUPPORT_FUNCTIONS(IFLeXClient, replay_protection_t)

  // default processing of unknown messages
  __always_inline static int _fallback(cell /*msg*/, slice /*msg_body*/) {
    return 0;
  }

private:
  __always_inline
  std::tuple<StateInit, address, uint256> preparePrice(
      uint128 price, uint128 min_amount, uint8 deals_limit, Tip3Config tip3cfg, cell price_code) const {
    DPrice price_data {
      .price_ = price,
      .sells_amount_ = uint128(0),
      .buys_amount_ = uint128(0),
      .flex_ = flex_,
      .min_amount_ = min_amount,
      .deals_limit_ = deals_limit,
      .notify_addr_ = notify_addr_,
      .workchain_id_ = workchain_id_,
      .tons_cfg_ = tons_cfg_,
      .tip3cfg_ = tip3cfg,
      .sells_ = {},
      .buys_ = {}
    };
    auto [state_init, std_addr] = prepare_price_state_init_and_addr(price_data, price_code);
    auto addr = address::make_std(workchain_id_, std_addr);
    return { state_init, addr, std_addr };
  }
};

DEFINE_JSON_ABI(IFLeXClient, DFLeXClient, EFLeXClient);

// ----------------------------- Main entry functions ---------------------- //
DEFAULT_MAIN_ENTRY_FUNCTIONS(FLeXClient, IFLeXClient, DFLeXClient, TIMESTAMP_DELAY)


