#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

#include <algorithm>
#include <random>

std::random_device rd;
std::mt19937 rng(rd());

struct Rule {
	std::string text;
	std::string key;

	// Some value specific to the rule
	int value = 0;
	bool tick_on_end = true; // tick at the end of a turn
	bool tick_on_play = false; // tick after a card is played (play animation completed)
};

std::map<std::string, Rule> possible_rules = {
	{"no_unplay", {"No take backs", "no_unplay", 7, false, true}},
	{"monochrome", {"Monochromatic", "monochrome", 2}},
	{"double_length", {"2x length score", "double_length", 1}},
	{"double_number", {"2x number score", "double_number", 2}},
	{"double_letter", {"2x letter score", "double_letter", 2}},
	{"double_shape", {"2x shape score", "double_shape", 2}},
	{"double_color", {"2x color score ", "double_color", 2}},
	{"discard_to_deck", {"Discard to deck", "discard_to_deck", 1}},
	{"run_backwards", {"Run Backwards", "run_backwards", 3}},
	{"carbon_copy", {"Carbon copy", "carbon_copy", 1}},
	{"double_jump", {"Double jump", "double_jump", 1}},
	{"timed_turn", {"Hurry hurry!", "timed_turn", 3}},
};

std::map<std::string, Rule> enabled_rules;

void TickRule(const std::string& rule_name) {
	if (enabled_rules.count(rule_name)) {
		auto& r = enabled_rules.at(rule_name);
		r.value -= 1;
		if (r.value < 0) {
			enabled_rules.erase(rule_name);
		}
	}
}

bool RuleEnabled(const std::string& rule_name) {
	return enabled_rules.count(rule_name) > 0;
}

template <typename T>
T lerp(T v0, T v1, float t) {
	return (1 - t) * v0 + t * v1;
}
float Ease(float x) {
	return -(std::cos(3.1415926f * x) - 1) / 2;
}

void DrawRules(olc::PixelGameEngine* pge, const std::map<std::string, Rule>& rules) {
	float y_pos = 10.0f;
	float x_pos = 184.0f;
	float y_increment = 12.0f;

	if (rules.size() == 0) {
		std::string str = "No Special Rules";
		olc::vf2d str_size = pge->GetTextSize(str);
		olc::vf2d draw_pos = { x_pos - str_size.x / 2.0f, y_pos };
		pge->DrawStringDecal(draw_pos, str);
	}
	else {
		for (const auto& rule : rules) {
			olc::vf2d str_size = pge->GetTextSize(rule.second.text);
			olc::vf2d draw_pos = { x_pos - str_size.x / 2.0f, y_pos };
			pge->DrawStringDecal(draw_pos, rule.second.text);
			y_pos += y_increment;
		}
	}
}

bool PointInRect(const olc::vf2d point, const olc::vf2d& pos, const olc::vf2d& size) {
	if (point.x >= pos.x && point.y >= pos.y && point.x < pos.x + size.x && point.y < pos.y + size.y) {
		return true;
	}

	return false;
}

struct ShapePrimitive {
	std::vector<olc::vf2d> points;
	std::vector<olc::vf2d> uv;
};

struct Shape {
	ShapePrimitive* primitive;
	olc::Pixel color;
	int color_index;
};

olc::vf2d card_size = { 25.0f, 35.0f };
float fTurnStart = 0.0f;
float fTotalTime = 0.0f;

std::array<olc::Pixel, 7> card_colors = {
	olc::Pixel{142, 68, 173},
	olc::Pixel{41, 128, 185},
	olc::Pixel{93, 173, 226},
	olc::Pixel{39, 174, 96},
	olc::Pixel{241, 196, 15},
	olc::Pixel{230, 126, 34},
	olc::Pixel{231, 76, 60}
};

// These will be filled in automatically based on the card colors
std::array<olc::Pixel, 7> shape_colors;

struct Card {
	olc::vf2d size;
	Shape shape;
	olc::Pixel color;
	int number;
	char letter;
	olc::vf2d position;
	bool locked; // prevents taking back the card if played

	// pos is top-left position
	void Draw(olc::PixelGameEngine* pge, float dim = 1.0f) const {
		bool monochrome = RuleEnabled("monochrome");
		olc::Pixel shape_color = monochrome ? olc::VERY_DARK_GREY : shape.color;
		olc::Pixel card_color = monochrome ? olc::GREY : color;

		pge->FillRectDecal(position, size, card_color * dim);

		//draw the shape
		std::vector<olc::vf2d> points;
		for (const auto& p : shape.primitive->points) {
			auto pt = p + position + size / 2.0f;
			points.push_back(pt);
		}

		
		pge->DrawPolygonDecal(nullptr, points, shape.primitive->uv, shape_color * dim);

		olc::vf2d tl = { 2.0f, 2.0f };

		//draw the number
		pge->DrawStringDecal(position + tl, std::to_string(number), olc::WHITE * dim);

		//draw the letter
		pge->DrawStringDecal(position - tl + size - olc::vf2d{8.0f, 8.0f}, std::string{letter}, olc::WHITE * dim);
	}

	bool operator==(const Card& other) {
		return number == other.number && shape.primitive == other.shape.primitive && letter == other.letter && color == other.color;
	}
};

std::unordered_map<int, ShapePrimitive> shape_primitives;

ShapePrimitive MakePrimitive(int side_count, float shape_size = 10.0f) {
	std::vector<olc::vf2d> points;
	std::vector<olc::vf2d> uv;

	for (int i = 0; i < side_count; i++) {
		float val = (2.0f * 3.14159f) / side_count;
		points.push_back({ shape_size * sin(i * val), shape_size * cos(i * val) });
		uv.push_back({ (sin(i * val) + 1.0f) / 2.0f, (sin(i * val) + 1.0f) / 2.0f });
	}

	return { points, uv };
}

std::vector<Card> CreateDeck(int num_numbers, int num_letters, int num_shapes) {
	std::vector<Card> deck;
	deck.reserve(num_numbers * num_letters * num_shapes);

	int counter = 0;

	for (int n = 0; n < num_numbers; n++) {
		for (int l = 0; l < num_letters; l++) {
			for (int s = 0; s < num_shapes; s++) {
				Card c;
				c.shape.primitive = &shape_primitives[s + 3];
				c.number = n + 1;
				c.letter = "ABCDEFGHI"[l];
				c.size = card_size;

				auto color_index = counter % 7;
				c.shape.color = shape_colors[color_index];
				c.shape.color_index = color_index;
				c.color = card_colors[color_index];
				counter++;
				deck.push_back(c);
			}
		}
	}

	return deck;
}

// deck and previously used cards
std::vector<Card> the_deck;
std::vector<Card> the_discard;


// Checks if the choice card would be valid if played after the end_card
bool IsValid(const Card& end_card, const Card& choice) {
	int valid_count = 0;
	int req_diff = 1;

	req_diff *= RuleEnabled("run_backwards") ? -1 : 1;
	req_diff *= RuleEnabled("double_jump") ? 2 : 1;
	req_diff *= RuleEnabled("carbon_copy") ? 0 : 1;

	valid_count += (choice.letter == end_card.letter + req_diff) ? 1 : 0;
	valid_count += (choice.number == end_card.number + req_diff) ? 1 : 0;
	valid_count += (choice.shape.primitive->points.size() == end_card.shape.primitive->points.size() + req_diff) ? 1 : 0;

	// If monochrome is enabled color would only count if the required difference is also 0
	if (RuleEnabled("monochrome")) {
		valid_count += (req_diff == 0) ? 1 : 0;
	}
	else {
		valid_count += (choice.shape.color_index == end_card.shape.color_index + req_diff) ? 1 : 0;
	}

	return valid_count > 0;
}

// The cards that have already been played this round
struct InPlay {
	std::vector<Card> cards;
	olc::vf2d position = { 128.0f, 120.0f };

	void Add(Card c) {
		if (cards.size()) {
			cards.back().locked = true;
		}

		if (RuleEnabled("no_unplay")) {
			c.locked = true;
		}

		cards.push_back(c);

		int cards_in_play = cards.size();
		olc::vf2d start_pos = { position.x - cards_in_play * (card_size.x / 2.0f + 0.5f), position.y };
		olc::vf2d increment = { card_size.x + 1.0f, 0.0f };

		for (auto& c : cards) {
			c.position = start_pos;
			start_pos += increment;
		}
	}

	void Draw(olc::PixelGameEngine* pge) {
		int cards_in_hand = cards.size();

		for (const auto& c : cards) {
			c.Draw(pge, c.locked ? 0.3f : 1.0f);
		}
	}
};

InPlay in_play;

//Cards in hand
struct Hand {
	std::vector<Card> cards;
	int max_size = 7;
	olc::vf2d position = { 128.0f, 205.0f };

	void Add(Card c) {
		c.locked = false;
		cards.push_back(c);

		int cards_in_hand = cards.size();
		olc::vf2d start_pos = { position.x - cards_in_hand * (card_size.x / 2.0f + 0.5f), position.y };
		olc::vf2d increment = { card_size.x + 1.0f, 0.0f };

		for (auto& c : cards) {
			c.position = start_pos;
			start_pos += increment;
		}
	}

	void Draw(olc::PixelGameEngine* pge) {
		for (const auto& c : cards) {
			bool isValid = true;

			if (in_play.cards.size() > 0) {
				isValid = IsValid(in_play.cards.back(), c);
			}
			c.Draw(pge, isValid ? 1.0f : 0.3f);
		}
	}
};

void DrawColorPanel(olc::PixelGameEngine* pge, olc::vf2d center_top_pos) {
	int color_count = card_colors.size();
	olc::vf2d start_pos = { center_top_pos.x - color_count * 5.0f, center_top_pos.y };
	olc::vf2d increment = { 10.0f, 0.0f };

	bool monochrome = RuleEnabled("monochrome");

	for (const auto& c : card_colors) {
		pge->FillRectDecal(start_pos, { 10.0f, 10.0f }, monochrome ? olc::GREY : c);
		start_pos += increment;
	}
}

template<typename T>
int max_value(const T& container) {
	using VT = typename T::value_type;

	auto v = std::max_element(
		std::begin(container),
		std::end(container),
		[](const VT& p1, const VT& p2) {
			return p1.second < p2.second;
		}
	);

	return v != container.end() ? v->second : 0;
}

int Fib(int x) {
	return std::round(std::pow(1.618, x) / 2.236);
}

int Score(const std::vector<Card>& run) {
	int length_score = Fib(run.size()) * (RuleEnabled("double_length") ? 2 : 1);

	//Count occurences of each number, letter, shape, and color
	//A bonus is awarded for using many of the same
	std::unordered_map<int, int> number_counts;
	std::unordered_map<int, int> shape_counts;
	std::unordered_map<char, int> letter_counts;
	std::unordered_map<uint32_t, int> color_counts;

	for (const auto& c : run) {
		number_counts[c.number]++;
		shape_counts[c.shape.primitive->points.size()]++;
		letter_counts[c.letter]++;
		color_counts[c.color.n]++;
	}

	int number_score = (max_value(number_counts) - 1) * (RuleEnabled("double_number") ? 2 : 1);
	int shape_score = (max_value(shape_counts) - 1) * (RuleEnabled("double_shape") ? 2 : 1);
	int letter_score = (max_value(letter_counts) - 1) * (RuleEnabled("double_letter") ? 2 : 1);
	int color_score = (max_value(color_counts) - 1) * (RuleEnabled("double_color") ? 2 : 1);

	return length_score + number_score + shape_score + letter_score + color_score;
}

int game_length = 5;
// The players hand
Hand hand;
int score = 0;

// Index into hand.cards for the card just played, for animation
int card_played_index = -1;

// Draw an end turn button returning true if it was pressed
bool DrawEndButton(olc::PixelGameEngine* pge, bool button_active = false) {
	olc::vf2d button_pos = { 2.0f, 193.0f };
	olc::vf2d button_size = { 80.0f, 10.0f };

	pge->FillRectDecal(button_pos, button_size, button_active ? olc::DARK_GREY : olc::VERY_DARK_GREY);

	olc::vf2d text_size = pge->GetTextSize("End Turn");
	olc::vf2d scale = (button_size) / text_size;

	pge->DrawStringDecal(button_pos + olc::vf2d{ 0.5f, 0.5f }, "End Turn", olc::BLACK, scale);

	if (button_active) {
		if (pge->GetMouse(0).bPressed) {
			if (PointInRect(pge->GetMousePos(), button_pos, button_size)) {
				return true;
			}
		}
	}
	return false;
}

bool DrawDiscardButton(olc::PixelGameEngine* pge, bool button_active = true) {
	olc::vf2d button_pos = { 174.0f, 193.0f };
	olc::vf2d button_size = { 80.0f, 10.0f };

	pge->FillRectDecal(button_pos, button_size, button_active ? olc::DARK_GREY : olc::VERY_DARK_GREY);
	//pge->FillRectDecal(button_pos, button_size, olc::DARK_GREY);

	olc::vf2d text_size = pge->GetTextSize("Discard");
	olc::vf2d scale = (button_size) / text_size;

	pge->DrawStringDecal(button_pos + olc::vf2d{ 0.5f, 0.5f }, "Discard", olc::BLACK, scale);

	if (pge->GetMouse(0).bPressed) {
		if (PointInRect(pge->GetMousePos(), button_pos, button_size)) {
			return true;
		}
	}

	return false;
}

int TurnTimeLeft() {
	return 10 - static_cast<int>(std::floor(fTotalTime - fTurnStart));
}

void DrawNormalInterface(olc::PixelGameEngine* pge) {
	DrawColorPanel(pge, { 128.0f, 193.0f });

	in_play.Draw(pge);
	hand.Draw(pge);

	DrawRules(pge, enabled_rules);
	pge->DrawStringDecal({ 10.0f, 10.0f }, "Score: " + std::to_string(score));
	pge->DrawStringDecal({ 10.0f, 20.0f }, "Deck : " + std::to_string(the_deck.size()));
	if (RuleEnabled("timed_turn")) {
		pge->DrawStringDecal({ 10.0f, 30.0f }, "Time : " + std::to_string(TurnTimeLeft()));
	}
}

enum class GameState {
	NONE,
	START_SCREEN,
	GAME_START, // Start of a game.
	DRAW_CARDS, // Draw cards up to the hand limit
	PICK_CARD, //Pick cards to play
	END_TURN, //calcualte the score of the current play
	END_GAME, //End of the game, show final score
	ANIMATE_PLAY,
	ANIMATE_UNPLAY,
	LENGTH_SELECT,
	TUTORIAL,
};


struct State {
	olc::PixelGameEngine* pge;
	explicit State(olc::PixelGameEngine* pge_) : pge(pge_) {};
	virtual ~State() = default;
	virtual void EnterState() {};
	virtual GameState OnUserUpdate(float fElapsedTime) = 0;
	virtual void ExitState() {};
};

struct Button {
	std::string text;
	olc::vf2d pos;
	olc::vf2d size;
	olc::vf2d text_size;
	int value;
};

struct StartScreenState : public State {
	StartScreenState(olc::PixelGameEngine* pge) : State(pge) {};

	std::vector<Card> left_cards;
	std::vector<Card> right_cards;
	std::vector<Card> center_cards;


	void EnterState() override {
		olc::vf2d text_size = pge->GetTextSize("RUN");

		hand.cards.clear();
		the_deck.clear();
		in_play.cards.clear();
		enabled_rules.clear();

		olc::vf2d center = olc::vf2d{ 128.0f, 100.0f } - card_size / 2.0f;

		// Only need to generate the title cards the very first time
		if (!center_cards.size()) {
			center_cards = {
				{
					card_size,
					{
						&shape_primitives[3],
						shape_colors[6],
						6
					},
					card_colors[6], 1, 'R', center - olc::vf2d{card_size.x + 1.0f, 0.0f}
				},
				{
					card_size,
					{
						&shape_primitives[4],
						shape_colors[6],
						6
					},
					card_colors[6], 2, 'U', center
				},
				{
					card_size,
					{
						&shape_primitives[5],
						shape_colors[6],
						6
					},
					card_colors[6], 3, 'N', center + olc::vf2d{card_size.x + 1.0f, 0.0f}
				},
			};
			for (int i = 0; i < 6; i++) {
				Card c;
				c.size = card_size;
				c.color = card_colors[i];
				c.shape.primitive = &shape_primitives[i + 3];
				c.shape.color = shape_colors[i];
				c.shape.color_index = i;
				c.number = i + 1;
				c.letter = "ABCDEF"[i];
				c.position = olc::vf2d{ 0.0f + i * (89.5f / 6.0f), 82.5f};
				left_cards.push_back(c);
				c.position = olc::vf2d{ 231.0f - i * (89.5f / 6.0f), 82.5f };
				right_cards.push_back(c);
			}
		}


	}

	GameState OnUserUpdate(float fElapsedTime) override {
		GameState next_state = GameState::START_SCREEN;

		for (int i = 0; i < 6; i++) {
			left_cards[i].Draw(pge, (i + 1) * (1.0f / 7.0f));
			right_cards[i].Draw(pge, (i + 1) * (1.0f / 7.0f));
		}

		for (const auto& c : center_cards) {
			c.Draw(pge);
		}

		olc::vf2d button_pos = olc::vf2d{ pge->ScreenWidth() / 3.0f, pge->ScreenHeight() * 2.0f / 3.0f };
		olc::vf2d button_size = olc::vf2d{ pge->ScreenWidth() / 3.0f, pge->ScreenHeight() / 6.0f };

		pge->FillRectDecal(button_pos, button_size, olc::DARK_GREY);

		olc::vf2d text_size = pge->GetTextSize("Start");

		olc::vf2d scale = (button_size) / text_size;

		pge->DrawStringDecal(button_pos + olc::vf2d{ 2.0, 2.0 }, "Start", olc::BLACK, scale);

		// Tutorial button
		olc::vf2d tutorial_pos = olc::vf2d{ pge->ScreenWidth() / 3.0f, pge->ScreenHeight() * 5.0f / 6.0f + 2.0f };
		olc::vf2d tutorial_size = olc::vf2d{ pge->ScreenWidth() / 3.0f, pge->ScreenHeight() / 12.0f };
		pge->FillRectDecal(tutorial_pos, tutorial_size, olc::DARK_GREY);

		text_size = pge->GetTextSize("Tutorial");
		scale = (tutorial_size) / text_size;

		pge->DrawStringDecal(tutorial_pos + olc::vf2d{ 1.0, 1.0 }, "Tutorial", olc::BLACK, scale);

		if (pge->GetMouse(0).bPressed) {
			if (PointInRect(pge->GetMousePos(), button_pos, button_size)) {
				next_state = GameState::LENGTH_SELECT;
			}
			if (PointInRect(pge->GetMousePos(), tutorial_pos, tutorial_size)) {
				next_state = GameState::TUTORIAL;
			}
		}

		return next_state;
	}
};

struct GameStartState : public State {
	GameStartState(olc::PixelGameEngine* pge) : State(pge) {};

	void EnterState() override { 
		//Initialize the hand back to the default configuration
		hand.max_size = 7;
		hand.cards.clear();

		//Clear the deck and discard
		the_deck.clear();
		the_discard.clear();

		//Create a new deck with the default configuration
		the_deck = CreateDeck(game_length, game_length, game_length);

		//Shuffle the deck
		std::shuffle(std::begin(the_deck), std::end(the_deck), rng);
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		//hand.Draw(pge, olc::vf2d{ 128.0f, 205.0f });
		return GameState::DRAW_CARDS;
	}
};

struct DrawCardsState : public State {
	DrawCardsState(olc::PixelGameEngine* pge) : State(pge) {};

	void EnterState() override {
		fTurnStart = fTotalTime;
		int cards_to_draw = std::min(hand.max_size - hand.cards.size(), the_deck.size());

		for (int i = 0; i < cards_to_draw; i++) {
			hand.Add(the_deck.back());
			the_deck.pop_back();
		}
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		DrawEndButton(pge);
		DrawDiscardButton(pge);
		DrawNormalInterface(pge);
		
		if (hand.cards.size() < 3) {
			return GameState::END_GAME;
		}
		return GameState::PICK_CARD;
	}
};

struct PickCardState : public State {
	PickCardState(olc::PixelGameEngine* pge) : State(pge) {};

	void EnterState() override {
		
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		GameState next_state = GameState::PICK_CARD;

		int cards_in_hand = hand.cards.size();

		olc::vf2d start_pos = { 128.0f - cards_in_hand * card_size.x / 2.0f, 205.0f };
		olc::vf2d increment = { card_size.x, 0.0f };

		if (pge->GetMouse(0).bPressed) {
			for (int i = 0; i < hand.cards.size(); i++) {
				if (PointInRect(pge->GetMousePos(), hand.cards[i].position, card_size)) {
					if (in_play.cards.size() == 0 || IsValid(in_play.cards.back(), hand.cards[i])) {
						
						card_played_index = i;
						next_state = GameState::ANIMATE_PLAY;
					}
				}
			}
		}

		// If the user clicked on the last in play card, let them take it back
		if (in_play.cards.size() && !in_play.cards.back().locked && pge->GetMouse(0).bPressed) {
			if (PointInRect(pge->GetMousePos(), in_play.cards.back().position, card_size)) {
				next_state = GameState::ANIMATE_UNPLAY;
			}
		}

		// Draw an end turn button, if a long enough run has been made
		if (DrawEndButton(pge, in_play.cards.size() > 2)) {
			next_state = GameState::END_TURN;
		}

		// Draw a discard button ending a turn but granting no points and discarding the hand
		if (DrawDiscardButton(pge, true) || (RuleEnabled("timed_turn") && TurnTimeLeft() <= 0)) {
			if (RuleEnabled("discard_to_deck")) {
				for (auto& c : hand.cards) {
					the_deck.push_back(c);
				}
			}

			hand.cards.clear();
			next_state = GameState::END_TURN;
		}

		DrawNormalInterface(pge);

		return next_state;
	}
};

struct EndTurnState : public State{
	EndTurnState(olc::PixelGameEngine* pge) : State(pge) {}

	void EnterState() override {

		//At the end of every round, there is a base 33%% chance to gain or refresh a random rule
		//the chance lowers if there are more rules added
		int rand_val = std::uniform_int_distribution<>(0, 5 + enabled_rules.size())(rng);
		if (rand_val < 2) {
			// Select a rule at random
			rand_val = std::uniform_int_distribution<>(0, possible_rules.size() - 1)(rng);
			auto rule = possible_rules.begin();
			std::advance(rule, rand_val);
			enabled_rules[rule->second.key] = rule->second;
		}

		if (in_play.cards.size() > 2) {
			score += Score(in_play.cards);
		}

		if (RuleEnabled("discard_to_deck")) {
			for (auto& c : in_play.cards) {
				c.locked = false;
				the_deck.push_back(c);
				//Shuffle the deck
				std::shuffle(std::begin(the_deck), std::end(the_deck), rng);
			}
		}
		
		in_play.cards.clear();
	}

	GameState OnUserUpdate(float fElapseddTime) override {
		DrawEndButton(pge);
		DrawDiscardButton(pge);
		DrawNormalInterface(pge);

		return GameState::DRAW_CARDS;
	}

	void ExitState() override {
		for (const auto& [key, rule] : possible_rules) {
			if (rule.tick_on_end) {
				TickRule(rule.key);
			}
		}
	}
};

struct EndGameState : public State {
	EndGameState(olc::PixelGameEngine* pge) : State(pge) {};

	void EnterState() override {
		hand.cards.clear();
		in_play.cards.clear();
		the_deck.clear();
		enabled_rules.clear();
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		std::string final_score_str = "Final Score:";
		std::string score_str = std::to_string(score);

		olc::vf2d final_size = pge->GetTextSize(final_score_str);
		olc::vf2d final_score_str_pos = olc::vf2d{ 128.0f, 110.0f } - final_size / 2.0f;
		olc::vf2d score_size = pge->GetTextSize(score_str);
		olc::vf2d score_pos = olc::vf2d{ 128.0f, 120.0f } - score_size / 2.0f;

		pge->DrawStringDecal(final_score_str_pos, final_score_str);
		pge->DrawStringDecal(score_pos, score_str);
		// Draw a restart button
		{
			std::string restart = "Restart";
			olc::vf2d restart_size = pge->GetTextSize(restart);

			olc::vf2d button_pos = olc::vf2d{ 127.0f, 179.0f } - restart_size / 2.0f;
			olc::vf2d button_size = restart_size + olc::vf2d{ 2.0f, 2.0f };
			pge->FillRectDecal(button_pos, button_size, olc::DARK_GREY);

			pge->DrawStringDecal(button_pos + olc::vf2d{ 1.0f, 1.0f }, restart, olc::BLACK);

			if (pge->GetMouse(0).bPressed) {
				if (PointInRect(pge->GetMousePos(), button_pos, button_size)) {
					score = 0;
					return GameState::START_SCREEN;
				}
			}
		}

		return GameState::END_GAME;
	}
};

struct PlayCardAnimationState : public State {
	PlayCardAnimationState(olc::PixelGameEngine* pge) : State(pge) {};

	float fTotalTime = 0.0f;

	struct AnimationState {
		olc::vf2d start_pos;
		olc::vf2d end_pos;
		int index;
	};

	std::vector<AnimationState> hand_animation;
	std::vector<AnimationState> play_animation;

	void EnterState() override {
		fTotalTime = 0.0f;
		hand_animation.clear();
		play_animation.clear();

		//Figure out where all the in_play cards will be moving to.  The card being moved will be the last card.
		olc::vf2d position = { in_play.position.x - (in_play.cards.size() + 1) * (card_size.x / 2.0f + 0.5f), in_play.position.y };
		olc::vf2d increment = { card_size.x + 1.0f, 0.0f };
		for (int i = 0; i < in_play.cards.size(); i++) {
			AnimationState as;
			as.start_pos = in_play.cards[i].position;
			as.end_pos = position;
			as.index = i;
			position += increment;
			play_animation.push_back(as);
		}
		 // The ending position of the card being played
		olc::vf2d end_pos = position;


		// Figure out where all the cards in hand will be moving to.
		position = { hand.position.x - (hand.cards.size() - 1) * (card_size.x / 2.0f + 0.5f), hand.position.y};
		for (int i = 0; i < hand.cards.size(); i++) {
			AnimationState as;
			as.start_pos = hand.cards[i].position;
			//If this is the card that was played, its moving across the screen
			as.end_pos = i == card_played_index ? end_pos : position;
			as.index = i;
			//If this is the card that was played, don't bump the position
			position += i == card_played_index ? olc::vf2d{0.0f, 0.0f} : increment;
			hand_animation.push_back(as);
		}
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		GameState next_state = GameState::ANIMATE_PLAY;

		fTotalTime += 1.8f * fElapsedTime;

		for (auto& ani : hand_animation) {
			Card& c = hand.cards[ani.index];
			c.position = lerp(ani.start_pos, ani.end_pos, std::min(1.0f, Ease(fTotalTime)));
		}

		for (auto& ani : play_animation) {
			Card& c = in_play.cards[ani.index];
			c.position = lerp(ani.start_pos, ani.end_pos, std::min(1.0f, Ease(fTotalTime)));
		}

		if (fTotalTime >= 1.0f) {
			in_play.Add(hand.cards[card_played_index]);
			hand.cards.erase(hand.cards.begin() + card_played_index);
			next_state = GameState::PICK_CARD;
		}

		DrawEndButton(pge);
		DrawDiscardButton(pge);
		DrawNormalInterface(pge);

		return next_state;
	}

	void ExitState() override {
		for (const auto& [key, rule] : possible_rules) {
			if (rule.tick_on_play) {
				TickRule(rule.key);
			}
		}
	}
};

struct UnPlayCardAnimationState : public State {
	UnPlayCardAnimationState(olc::PixelGameEngine* pge) : State(pge) {};

	float fTotalTime = 0.0f;

	struct AnimationState {
		olc::vf2d start_pos;
		olc::vf2d end_pos;
		int index;
	};

	std::vector<AnimationState> hand_animation;
	std::vector<AnimationState> play_animation;

	void EnterState() override {
		fTotalTime = 0.0f;
		hand_animation.clear();
		play_animation.clear();

		//Figure out where all the in_play cards will be moving to.  The card being moved will be the last card.
		olc::vf2d position = { in_play.position.x - (in_play.cards.size() - 1) * (card_size.x / 2.0f + 0.5f), in_play.position.y };
		olc::vf2d increment = { card_size.x + 1.0f, 0.0f };
		for (int i = 0; i < in_play.cards.size(); i++) {
			AnimationState as;
			as.start_pos = in_play.cards[i].position;
			as.end_pos = position;
			as.index = i;
			position += increment;
			play_animation.push_back(as);
		}

		// Figure out where all the cards in hand will be moving to.
		position = { hand.position.x - (hand.cards.size() + 1) * (card_size.x / 2.0f + 0.5f), hand.position.y };
		for (int i = 0; i < hand.cards.size(); i++) {
			AnimationState as;
			as.start_pos = hand.cards[i].position;
			//If this is the card that was played, its moving across the screen
			as.end_pos = position;
			as.index = i;
			//If this is the card that was played, don't bump the position
			position += increment;
			hand_animation.push_back(as);
		}

		// Only the last card can be un-played, setup its new movement position
		play_animation.push_back(AnimationState{ in_play.cards.back().position, position, static_cast<int>(in_play.cards.size()) - 1 });
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		GameState next_state = GameState::ANIMATE_UNPLAY;

		fTotalTime += 1.8f * fElapsedTime;

		for (auto& ani : hand_animation) {
			Card& c = hand.cards[ani.index];
			c.position = lerp(ani.start_pos, ani.end_pos, std::min(1.0f, Ease(fTotalTime)));
		}

		for (auto& ani : play_animation) {
			Card& c = in_play.cards[ani.index];
			c.position = lerp(ani.start_pos, ani.end_pos, std::min(1.0f, Ease(fTotalTime)));
		}

		if (fTotalTime >= 1.0f) {
			hand.Add(in_play.cards.back());
			in_play.cards.pop_back();
			next_state = GameState::PICK_CARD;
		}

		DrawEndButton(pge);
		DrawDiscardButton(pge);
		DrawNormalInterface(pge);

		return next_state;
	}
};

struct LengthSelectState : public State {

	std::array<Button, 5> buttons;

	LengthSelectState(olc::PixelGameEngine* pge) : State(pge) {
		//Setup the buttons
		buttons[0].text = "Normal";
		buttons[0].pos = { 88.0f, 91.0f };
		buttons[0].size = { 80.0f, 10.0f };
		buttons[0].text_size = pge->GetTextSize(buttons[0].text);
		buttons[0].value = 5;

		buttons[1].text = "Medium";
		buttons[1].pos = { 88.0f, 103.0f };
		buttons[1].size = { 80.0f, 10.0f };
		buttons[1].text_size = pge->GetTextSize(buttons[1].text);
		buttons[1].value = 6;

		buttons[2].text = "Long";
		buttons[2].pos = { 88.0f, 115.0f };
		buttons[2].size = { 80.0f, 10.0f };
		buttons[2].text_size = pge->GetTextSize(buttons[2].text);
		buttons[2].value = 7;

		buttons[3].text = "Too Long";
		buttons[3].pos = { 88.0f, 127.0f };
		buttons[3].size = { 80.0f, 10.0f };
		buttons[3].text_size = pge->GetTextSize(buttons[3].text);
		buttons[3].value = 9;

		buttons[4].text = "Back";
		buttons[4].pos = { 88.0f, 139.0f };
		buttons[4].size = { 80.0f, 10.0f };
		buttons[4].text_size = pge->GetTextSize(buttons[4].text);
		buttons[4].value = 0;
	};

	GameState OnUserUpdate(float fElapsedTime) override {
		GameState next_state = GameState::LENGTH_SELECT;

		for (int i = 0; i < buttons.size(); i++) {
			pge->FillRectDecal(buttons[i].pos, buttons[i].size, olc::DARK_GREY);
			olc::vf2d text_pos = buttons[i].pos + buttons[i].size / 2.0f - buttons[i].text_size / 2.0f;
			pge->DrawStringDecal(text_pos, buttons[i].text, olc::BLACK);
			if (pge->GetMouse(0).bPressed && PointInRect(pge->GetMousePos(), buttons[i].pos, buttons[i].size)) {
				game_length = buttons[i].value;
				next_state = game_length != 0 ? GameState::GAME_START : GameState::START_SCREEN;
			}
		}

		return next_state;
	}
};

struct TutorialState : public State {
	TutorialState(olc::PixelGameEngine* pge) : State(pge) {};

	struct TextData {
		olc::vf2d pos;
		std::string str;
		olc::Pixel color = olc::WHITE;
	};

	struct RectData {
		olc::vf2d pos;
		olc::vf2d size;
		olc::Pixel color = olc::YELLOW;
	};

	struct LineData {
		olc::vf2d pos_a;
		olc::vf2d pos_b;
		olc::Pixel color = olc::WHITE;
	};

	struct TutorialData {
		bool draw_hand;
		bool draw_in_play;
		bool draw_end_turn;
		bool draw_discard;
		bool draw_color_track;

		std::vector<TextData> text;
		std::vector<RectData> rects;
		std::vector<LineData> lines;
	};

	std::mt19937 tutorial_rng;

	int tutorial_id = 0;

	std::vector<TutorialData> tutorial_data = {
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"Your objective is to play runs"}},
				{{10.0f, 20.0f}, std::string{"of cards from your hand.  Long"}},
				{{10.0f, 30.0f}, std::string{"runs are worth more points."}},
				{{10.0f, 40.0f}, std::string{"Each card has 4 main values."}},
				{{53.0f, 108.0f}, std::string{"Number"}},
				{{165.0f, 147.0f}, std::string{"Letter"}},
				{{53.0f, 147.0f}, std::string{"Shape"}},
				{{165.0f, 108.0f}, std::string{"Color"}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{},
			{
				{{100.0f, 113.0f}, {116.0f, 121.0f}},
				{{93.0f,  151.0f}, {120.0f, 143.0f}},
				{{165.0f, 115.0f}, {138.0f, 124.0f}},
				{{163.0f, 151.0f}, {138.0f, 151.0f}},
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"To count as a run only one of"}},
				{{10.0f, 20.0f}, std::string{"these values needs to increment"}},
				{{10.0f, 30.0f}, std::string{"from card to card."}},
				{{53.0f, 108.0f}, std::string{"Number"}},
				{{165.0f, 147.0f}, std::string{"Letter"}},
				{{53.0f, 147.0f}, std::string{"Shape"}},
				{{165.0f, 108.0f}, std::string{"Color"}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{},
			{
				{{100.0f, 113.0f}, {116.0f, 121.0f}},
				{{93.0f,  151.0f}, {120.0f, 143.0f}},
				{{165.0f, 115.0f}, {138.0f, 124.0f}},
				{{163.0f, 151.0f}, {138.0f, 151.0f}},
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"The color track at the bottom"}},
				{{10.0f, 20.0f}, std::string{"of the screen shows the order"}},
				{{10.0f, 30.0f}, std::string{"of colors from lowest value on"}},
				{{10.0f, 40.0f}, std::string{"the left to highest value on"}},
				{{10.0f, 50.0f}, std::string{"the right."}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{92.0f, 192.0f}, {72.0f, 12.0f}, olc::YELLOW}
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"The current run is here in the"}},
				{{10.0f, 20.0f}, std::string{"middle of the screen."}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{6.0f, 117.0f}, {243.0f, 41.0f}, olc::YELLOW}
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"Your current hand is at the"}},
				{{10.0f, 20.0f}, std::string{"bottom of the screen."}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{6.0f, 200.0f}, {243.0f, 41.0f}, olc::YELLOW}
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"You can unplay the last card"}},
				{{10.0f, 20.0f}, std::string{"of the run and return it to"}},
				{{10.0f, 30.0f}, std::string{"your hand."}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{140.0f, 119.0f}, {27.0f, 37.0f}, olc::YELLOW},
			}
		},
		{
			true, true, true, false, true,
			{
				{{10.0f, 10.0f}, std::string{"If you have a run of length"}},
				{{10.0f, 20.0f}, std::string{"at least 3 you may end your"}},
				{{10.0f, 30.0f}, std::string{"turn and score the run with"}},
				{{10.0f, 40.0f}, std::string{"the end turn button."}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{1.0f, 191.0f}, {82.0f, 13.0f}, olc::YELLOW}
			}
		},
		{
			true, true, false, true, true,
			{
				{{10.0f, 10.0f}, std::string{"You may discard your hand at"}},
				{{10.0f, 20.0f}, std::string{"any time with the discard"}},
				{{10.0f, 30.0f}, std::string{"button.  This throws away all"}},
				{{10.0f, 40.0f}, std::string{"cards in your hand and draws"}},
				{{10.0f, 50.0f}, std::string{"new cards on the next turn."}},
				{{10.0f, 60.0f}, std::string{"If a valid run is present then"}},
				{{10.0f, 70.0f}, std::string{"it will still be scored."}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{172.0f, 191.0f}, {84.0f, 13.0f}, olc::YELLOW}
			}
		},
		{
			true, true, false, true, true,
			{
				{{10.0f, 10.0f}, std::string{"In either case you will draw"}},
				{{10.0f, 20.0f}, std::string{"back up to your maximum hand"}},
				{{10.0f, 30.0f}, std::string{"size and begin a new turn."}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
		},
		{
			true, true, false, true, true,
			{
				{{10.0f, 10.0f}, std::string{"Score: 0"}},
				{{10.0f, 20.0f}, std::string{"Deck : 118"}},
				{{10.0f, 30.0f}, std::string{"The current score and number"}},
				{{10.0f, 40.0f}, std::string{"of cards left in the deck are"}},
				{{10.0f, 50.0f}, std::string{"both shown in the top left."}},
				{{10.0f, 60.0f}, std::string{"The game ends when the deck is"}},
				{{10.0f, 70.0f}, std::string{"empty and no run can be made."}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{8.0f, 8.0f}, {84.0f, 22.0f}, olc::YELLOW}
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"Runs are scored primarily on"}},
				{{10.0f, 20.0f}, std::string{"their length.  A run of 3 has"}},
				{{10.0f, 30.0f}, std::string{"a base score of 2 while a run"}},
				{{10.0f, 40.0f}, std::string{"of 6 has a base score of 8."}},
				{{10.0f, 50.0f}, std::string{"Repeating card values within a"}},
				{{10.0f, 60.0f}, std::string{"run gives a point bonus"}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"This run has a length of 3 for"}},
				{{10.0f, 20.0f}, std::string{"a base score of 2."}},
				{{2.0f, 110.0f}, std::string{"Length - 2"}},
				{{10.0f, 160.0f}, std::string{"Total - 2"}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{88.0f, 118.0f}, {80.0f, 39.0f}, olc::YELLOW}
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"No number appears more than"}},
				{{10.0f, 20.0f}, std::string{"one time.  The number bonus"}},
				{{10.0f, 30.0f}, std::string{"is 0 points."}},
				{{2.0f, 110.0f}, std::string{"Length - 2"}},
				{{2.0f, 120.0f}, std::string{"Number - 0"}},
				{{10.0f, 160.0f}, std::string{"Total - 2"}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{90.0f, 121.0f}, {10.0f, 10.0f}, olc::YELLOW},
				{{116.0f, 121.0f}, {10.0f, 10.0f}, olc::YELLOW},
				{{142.0f, 121.0f}, {10.0f, 10.0f}, olc::YELLOW},
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"No letter appears more than"}},
				{{10.0f, 20.0f}, std::string{"one time.  The letter bonus"}},
				{{10.0f, 30.0f}, std::string{"is 0 points."}},
				{{2.0f, 110.0f}, std::string{"Length - 2"}},
				{{2.0f, 120.0f}, std::string{"Number - 0"}},
				{{2.0f, 130.0f}, std::string{"Letter - 0"}},
				{{10.0f, 160.0f}, std::string{"Total - 2"}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{103.0f, 144.0f}, {10.0f, 10.0f}, olc::YELLOW},
				{{129.0f, 144.0f}, {10.0f, 10.0f}, olc::YELLOW},
				{{155.0f, 144.0f}, {10.0f, 10.0f}, olc::YELLOW},
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"A heptagon is the most common"}},
				{{10.0f, 20.0f}, std::string{"shape; appearing 2 times.  The"}},
				{{10.0f, 30.0f}, std::string{"shape bonus is 1 point."}},
				{{2.0f, 110.0f}, std::string{"Length - 2"}},
				{{2.0f, 120.0f}, std::string{"Number - 0"}},
				{{2.0f, 130.0f}, std::string{"Letter - 0"}},
				{{10.0f, 140.0f}, std::string{"Shape - 1"}},
				{{10.0f, 160.0f}, std::string{"Total - 3"}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{91.0f, 127.0f}, {21.0f, 21.0f}, olc::YELLOW},
				{{143.0f, 127.0f}, {21.0f, 21.0f}, olc::YELLOW},
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"The most common card color is "}},
				{{10.0f, 20.0f}, std::string{"green; appearing 2 times."}},
				{{10.0f, 30.0f}, std::string{"The color bonus is 1 point."}},
				{{2.0f, 110.0f}, std::string{"Length - 2"}},
				{{2.0f, 120.0f}, std::string{"Number - 0"}},
				{{2.0f, 130.0f}, std::string{"Letter - 0"}},
				{{10.0f, 140.0f}, std::string{"Shape - 1"}},
				{{10.0f, 150.0f}, std::string{"Color - 1"}},
				{{10.0f, 160.0f}, std::string{"Total - 4"}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
			{
				{{88.0f, 119.0f}, { 27.0f, 37.0f }, olc::YELLOW},
				{ {140.0f, 119.0f}, {27.0f, 37.0f}, olc::YELLOW },
			}
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 10.0f}, std::string{"The total score for this run"}},
				{{10.0f, 20.0f}, std::string{"is 4 points."}},
				{{2.0f, 110.0f}, std::string{"Length - 2"}},
				{{2.0f, 120.0f}, std::string{"Number - 0"}},
				{{2.0f, 130.0f}, std::string{"Letter - 0"}},
				{{10.0f, 140.0f}, std::string{"Shape - 1"}},
				{{10.0f, 150.0f}, std::string{"Color - 1"}},
				{{10.0f, 160.0f}, std::string{"Total - 4"}},
				{{10.0f, 170.0f}, std::string{"Click to continue"}},
			},
		},
		{
			true, true, false, false, true,
			{
				{{10.0f, 40.0f}, std::string{"On occasion additional game"}},
				{{10.0f, 50.0f}, std::string{"rules will be added.  These"}},
				{{10.0f, 60.0f}, std::string{"are shown in the top right and"}},
				{{10.0f, 70.0f}, std::string{"do what they say."}},
				{{10.0f, 170.0f}, std::string{"Click to return to title"}},
			},
			{
				{{128.0f, 10.0f}, {126.0f, 24.0f}, olc::YELLOW}
			}
		},
	};



	void EnterState() override {
		tutorial_rng = {};
		tutorial_rng.seed(10032);
		the_deck = CreateDeck(5, 5, 5);

		auto deck_copy = the_deck;
		//shuffle(std::begin(the_deck), std::end(the_deck), tutorial_rng);

		//std::vector<int> indices;

		//for (int i = 0; i < 7; i++) {
		//	auto loc = std::find(std::begin(deck_copy), std::end(deck_copy), the_deck[the_deck.size() - i - 1]);
		//	indices.push_back(loc - std::begin(deck_copy));
		//}

		//// Draw the cards into the tutorial hand
		//int cards_to_draw = std::min(hand.max_size - hand.cards.size(), the_deck.size());

		//for (int i = 0; i < cards_to_draw; i++) {
		//	hand.Add(the_deck.back());
		//	the_deck.pop_back();
		//}

		// Shuffling is not stable across platforms.  This normally doesn't matter but the tutorial
		// needs specific cards for the examples.
		std::array<int, 7> hand_card_indices = {59, 91, 24, 54, 36, 90, 109};
		for (const auto& index : hand_card_indices) {
			hand.Add(*(std::begin(the_deck) + index));
		}
		
		std::sort(std::begin(hand_card_indices), std::end(hand_card_indices), std::greater<int>());
		for (const auto& index : hand_card_indices) {
			the_deck.erase(std::begin(the_deck) + index);
		}

		// Shuffle the deck now just in case it is needed
		shuffle(std::begin(the_deck), std::end(the_deck), tutorial_rng);

		in_play.Add(hand.cards[0]);
		hand.cards.erase(std::begin(hand.cards));

		tutorial_id = 0;
	}

	GameState OnUserUpdate(float fElapsedTime) override {
		GameState next_state = GameState::TUTORIAL;
		const auto& td = tutorial_data[tutorial_id];
		if (td.draw_in_play) {
			in_play.Draw(pge);
		}

		if (td.draw_hand) {
			hand.Draw(pge);
		}

		if (td.draw_color_track) {
			DrawColorPanel(pge, { 128.0f, 193.0f });
		}

		if (td.draw_end_turn) {
			olc::vf2d button_pos = { 2.0f, 193.0f };
			olc::vf2d button_size = { 80.0f, 10.0f };

			pge->FillRectDecal(button_pos, button_size, olc::DARK_GREY);

			olc::vf2d text_size = pge->GetTextSize("End Turn");
			olc::vf2d scale = (button_size) / text_size;

			pge->DrawStringDecal(button_pos + olc::vf2d{ 0.5f, 0.5f }, "End Turn", olc::BLACK, scale);
		}

		if (td.draw_discard) {
			olc::vf2d button_pos = { 174.0f, 193.0f };
			olc::vf2d button_size = { 80.0f, 10.0f };

			pge->FillRectDecal(button_pos, button_size, olc::DARK_GREY);

			olc::vf2d text_size = pge->GetTextSize("Discard");
			olc::vf2d scale = (button_size) / text_size;

			pge->DrawStringDecal(button_pos + olc::vf2d{ 0.5f, 0.5f }, "Discard", olc::BLACK, scale);
		}

		for (const auto& rect : td.rects) {
			pge->DrawRectDecal(rect.pos, rect.size, rect.color);
		}

		for (const auto& line : td.lines) {
			pge->DrawLineDecal(line.pos_a, line.pos_b, line.color);
		}

		for (const auto& text : td.text) {
			pge->DrawStringDecal(text.pos, text.str, text.color);
		}

		if (pge->GetMouse(0).bPressed) {
			if (tutorial_id < tutorial_data.size() - 1) {
				tutorial_id++;
				if (tutorial_id == 3) {
					in_play.Add(hand.cards[0]);
					hand.cards.erase(std::begin(hand.cards));
					in_play.Add(hand.cards[0]);
					hand.cards.erase(std::begin(hand.cards));
				}
			}
			else {
				next_state = GameState::START_SCREEN;
			}
		}

		return next_state;
	}
};

std::map<GameState, std::unique_ptr<State>> gameStates;


class Run : public olc::PixelGameEngine
{
public:
	Run()
	{
		sAppName = "Run";

	}
	GameState current_state = GameState::START_SCREEN;
	GameState next_state = GameState::START_SCREEN;
	GameState prev_state = GameState::NONE;

public:
	bool OnUserCreate() override
	{
		gameStates.insert(std::make_pair(GameState::START_SCREEN, std::make_unique<StartScreenState>(this)));
		gameStates.insert(std::make_pair(GameState::GAME_START, std::make_unique<GameStartState>(this)));
		gameStates.insert(std::make_pair(GameState::DRAW_CARDS, std::make_unique<DrawCardsState>(this)));
		gameStates.insert(std::make_pair(GameState::PICK_CARD, std::make_unique<PickCardState>(this)));
		gameStates.insert(std::make_pair(GameState::END_GAME, std::make_unique<EndGameState>(this)));
		gameStates.insert(std::make_pair(GameState::ANIMATE_PLAY, std::make_unique<PlayCardAnimationState>(this)));
		gameStates.insert(std::make_pair(GameState::ANIMATE_UNPLAY, std::make_unique<UnPlayCardAnimationState>(this)));
		gameStates.insert(std::make_pair(GameState::LENGTH_SELECT, std::make_unique<LengthSelectState>(this)));
		gameStates.insert(std::make_pair(GameState::END_TURN, std::make_unique<EndTurnState>(this)));
		gameStates.insert(std::make_pair(GameState::TUTORIAL, std::make_unique<TutorialState>(this)));

		for (int i = 3; i <= 11; i++) {
			shape_primitives[i] = MakePrimitive(i);
		}

		for (int i = 0; i < card_colors.size(); i++) {
			shape_colors[i] = card_colors[i] * 0.6;
		}
		
		return true;
	}

	int side_count = 3;
	int deck_index = 0;

	bool OnUserUpdate(float fElapsedTime) override
	{
		fTotalTime += fElapsedTime;
		const auto& state = gameStates.at(current_state);

		if (current_state != prev_state) {
			state->EnterState();
		}

		next_state = state->OnUserUpdate(fElapsedTime);

		if (next_state != current_state) {
			state->ExitState();
		}

		prev_state = current_state;
		current_state = next_state;

		return true;
	}
};


int main()
{
	Run the_game; // you have lost it
	if (the_game.Construct(256, 240, 4, 4, false, true))
		the_game.Start();

	return 0;
}