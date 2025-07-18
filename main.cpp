#include <exception>
#include <utility>
#include <memory>
#include <new>
#include <ranges>
#include <charconv>
#include <cctype>

#include <tgbm/bot.hpp>
#include <tgbm/utils/formatters.hpp>
#include <tgbm/utils/scope_exit.hpp>

#include "include/database.h"

#define LIFT(func) [] (auto&&... args) { return func(std::forward<decltype(args)>(args)...); }

constexpr std::string_view kCallbackMarkActivePrefix = "mark_active ";


static tgbm::api::InlineKeyboardMarkup GetDataCollectorMessage(int64_t id) {
  using namespace tgbm::api;
  using button = InlineKeyboardButton;
  InlineKeyboardMarkup markup;

  markup.inline_keyboard = {
      {{.text = "Отметить", .data = button::callback_data{.value = fmt::format("mark_active {}", id)}}},
  };
  return markup;
}

static std::optional<int64_t> ParseMarkActiveQuery(tgbm::api::optional<std::string> &query) {
  if (query.has_value()) {
    std::string_view str = query.value();
    if (str.starts_with(kCallbackMarkActivePrefix)) {
      int64_t ans;
      if (std::from_chars(str.data() + kCallbackMarkActivePrefix.size() , str.data() + str.size(), ans).ec != std::errc()) {
        return std::nullopt;
      }
      return ans;
    } else {
      return std::nullopt;
    }
  } else {
    return std::nullopt;
  }
}

static dd::task<void> AnswerCallbackQuery(tgbm::bot& bot, tgbm::api::CallbackQuery q) {
  using namespace tgbm::api;

  int64_t act_check_id;
  if (auto id_opt = ParseMarkActiveQuery(q.data); id_opt.has_value()) {
    act_check_id = id_opt.value();
  } else {
    co_await bot.api.answerCallbackQuery({
      .callback_query_id = q.id,
        .text = fmt::format("Некорректный формат текста обратного вызова кнопки: \"{}\"", q.data.value_or("<empty>"))
    });
    co_return;
  }

  std::string_view message = GetDb().AddActivity(act_check_id, q.from->id, fmt::format("{} {}", q.from->first_name, q.from->last_name.value_or("")), std::chrono::system_clock::now());

  bool success = co_await bot.api.answerCallbackQuery({
    .callback_query_id = q.id,
    .text = std::string(message)
  });
  if (!success) {
    TGBM_LOG_ERROR("bot cannot handle callback query =(");
    co_return;
  }
  co_return;
}

static dd::task<void> AnswerInlineQuery(tgbm::bot& bot, tgbm::api::InlineQuery q) {
  using namespace tgbm::api;

  int64_t act_id = GetDb().AddActivityCheck(q.from->id);

  InlineQueryResultArticle res {
    .id = std::to_string(act_id),
    .title = "Создать проверку активности",
    .description = "",
    .input_message_content = InputTextMessageContent{
      .message_text = fmt::format("Отметьте свою активность", act_id)
    },
    .reply_markup = GetDataCollectorMessage(act_id)
  };

  bool succed = co_await bot.api.answerInlineQuery({
    .inline_query_id = q.id,
    .results = { InlineQueryResult{res} },
    .cache_time = 0
  });

  if (!succed) {
    TGBM_LOG_ERROR("bot cannot handle inline query =(");
    co_return;
  }
  co_return;
}

static dd::task<void> AnswerInlineChosen(tgbm::bot& bot, tgbm::api::ChosenInlineResult q) {
  using namespace tgbm::api;

  co_await bot.api.sendMessage({
    .chat_id = q.from->id.value,
    .text = fmt::format("ID проверки активности: `{}`", q.result_id),
    .parse_mode = "MarkdownV2"
  });
  co_return;
}

dd::task<void> StartMainTask(tgbm::bot& bot) {
  on_scope_exit {
    // stop bot on failure
    bot.stop();
  };
  fmt::println("launching echobot, info: {}", co_await bot.api.getMe());

  co_foreach(tgbm::api::Update && u, bot.updates()) {
    TGBM_LOG_DEBUG("got something");
    if (auto *cq = u.get_callback_query(); cq) {
      AnswerCallbackQuery(bot, std::move(*cq)).start_and_detach();
    } else if (auto *iq = u.get_inline_query(); iq) {
      AnswerInlineQuery  (bot, std::move(*iq)).start_and_detach();
    } else if (auto *cir = u.get_chosen_inline_result(); cir) {
      AnswerInlineChosen(bot, std::move(*cir)).start_and_detach();
    } else {
      TGBM_LOG_DEBUG("passed upd: {}", u.discriminator_now());
    }
  }
  co_return;
}

static dd::task<void> HandleGetResultCommand(tgbm::bot &bot, tgbm::api::Integer chat_id, tgbm::api::optional<std::string> text) {
  static constexpr std::string_view kGetResultCommand = "get_result";
  if (!text.has_value()) [[unlikely]] {
    TGBM_LOG_ERROR("empty command text for get_result, idk how is it possible");
    co_return;
  }
  std::string_view text_sv = text.value();
  std::string_view num_sv = text_sv.substr(text_sv.find(kGetResultCommand) + kGetResultCommand.size());
  num_sv.remove_prefix(std::find_if(num_sv.begin(), num_sv.end(), LIFT(std::isdigit)) - num_sv.begin());
  {
    size_t cnt = 0;
    while (cnt < num_sv.size() && std::isdigit(num_sv[cnt])) {
      ++cnt;
    }
    num_sv = num_sv.substr(0, cnt);
  }
  int64_t num;
  if (std::from_chars(num_sv.begin(), num_sv.end(), num).ec != std::errc()) {
    co_await bot.api.sendMessage({
      .chat_id = chat_id,
      .text = "Некорректный формат команды, попробуйте /get_result <ID сбора активности>"
    });
    TGBM_LOG_DEBUG("invalid number format for get_result: \"{}\"", num_sv);
    co_return;
  }
  if (auto path_or_err = GetDb().GetActivityCheckList(num, chat_id); path_or_err.has_value()) {
    co_await bot.api.sendDocument({
      .chat_id = chat_id,
      .document = tgbm::api::InputFile::from_file(path_or_err.value(), "text/csv")
    });
  } else {
    co_await bot.api.sendMessage({
      .chat_id = chat_id,
      .text = std::string(path_or_err.error())
    });
  }
  co_return;
}

dd::task<void> HandleHelpCommand(tgbm::bot &bot, tgbm::api::Integer chat_id) {
  static constexpr std::string_view kInfo =
"- Создание проверки активности: если вы хотите создать проверку активности, наберите тэг бота в чате, куда хотите отправить сообщение и нажмите на появившуюся кнопку \"Создать проверку активности\". В чат будет отправлено сообщение с кнопкой для учета активности, а вам в личные сообщения - уникальный ID этой проверки активности.\n"
"- Отметка активности: нажмите на кнопку под сообщением с проверкой активности. После этого появится модально окно с надписью \"Спасибо, ваша активность учтена\". В случае, если вы нажмете на кнопку повторно, появится модальное окно с надписью \"Спасибо, но ваша активность уже была учтена ранее\"\n"
"- Сбор результатов проверки активности: чтобы получить результаты проверки активности отправьте в личные сообщения боту сообщение вида \"/get_result <ID проверки>\", вам будет выслан csv-файл.\n";
  co_await bot.api.sendMessage({
    .chat_id = chat_id,
    .text = std::string(kInfo)
  });
  co_return;
}

int main() {
  const char* token = std::getenv("BOT_TOKEN");
  if (!token) {
    fmt::println("launching telegram bot requires bot token from @BotFather");
    return -1;
  }

  tgbm::bot bot{token /*"api.telegram.org", "some_ssl_certificate"*/};

  bot.commands.add("get_result", [&bot](tgbm::api::Message&& m) {
    HandleGetResultCommand(bot, m.chat->id, std::move(m.text)).start_and_detach();
  });
  bot.commands.add("help", [&bot](tgbm::api::Message&& m) {
    HandleHelpCommand(bot, m.chat->id).start_and_detach();
  });

  StartMainTask(bot).start_and_detach();
  bot.run();

  return 0;
}
