#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

#include <algorithm>
#include <random>


// Number 5 numbers - 1 2 3 4 5
// Letter 5 letters - A B C D E
// Shape 5 Shapes   - 1 2 3 4 5 sides
// Color Not used
//

// TODO
// game length selection
// short - 5, 5, 5
// medium, 6, 6, 6
// long,   7, 7, 7
// eternal 9, 9, 9
// 
// 
// rules changes
//    first card matches X
//    each card must match some attribute
//    cannot unplay
//    cards change
//    draw from the discard
//

std::random_device rd;
std::mt19937 rng(rd());




template <typename T>
T lerp(T v0, T v1, float t) {
	return (1 - t) * v0 + t * v1;
}
float Ease(float x) {
	return -(std::cos(3.1415926f * x) - 1) / 2;
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
};

olc::vf2d card_size = { 25.0f, 35.0f };
std::array<olc::Pixel, 7> card_colors = { olc::BLUE, olc::DARK_GREEN, olc::RED, olc::MAGENTA, olc::DARK_YELLOW, olc::GREY, olc::DARK_CYAN };
std::array<olc::Pixel, 7> shape_colors = { olc::VERY_DARK_BLUE, olc::VERY_DARK_GREEN, olc::VERY_DARK_RED, olc::VERY_DARK_MAGENTA, olc::VERY_DARK_YELLOW, olc::VERY_DARK_GREY, olc::VERY_DARK_CYAN };

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
		pge->FillRectDecal(position, size, color * dim);

		//draw the shape
		std::vector<olc::vf2d> points;
		for (const auto& p : shape.primitive->points) {
			auto pt = p + position + size / 2.0f;
			points.push_back(pt);
		}

		pge->DrawPolygonDecal(nullptr, points, shape.primitive->uv, shape.color * dim);

		olc::vf2d tl = { 2.0f, 2.0f };

		//draw the number
		pge->DrawStringDecal(position + tl, std::to_string(number), olc::WHITE * dim);

		//draw the letter
		pge->DrawStringDecal(position - tl + size - olc::vf2d{8.0f, 8.0f}, std::string{letter}, olc::WHITE * dim);
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
	if (choice.letter == end_card.letter + 1) {
		return true;
	}

	if (choice.number == end_card.number + 1) {
		return true;
	}

	if (choice.shape.primitive->points.size() == end_card.shape.primitive->points.size() + 1) {
		return true;
	}

	return false;
}

// The cards that have already been played this round
struct InPlay {
	std::vector<Card> cards;
	olc::vf2d position = { 128.0f, 120.0f };

	void Add(Card c) {
		if (cards.size()) {
			cards.back().locked = true;
		}
		cards.push_back(c);

		int cards_in_play = cards.size();
		olc::vf2d start_pos = { position.x - cards_in_play * card_size.x / 2.0f, position.y };
		olc::vf2d increment = { card_size.x, 0.0f };

		for (auto& c : cards) {
			c.position = start_pos;
			start_pos += increment;
		}
	}

	// pos is the "center top position" of the hand
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
		olc::vf2d start_pos = { position.x - cards_in_hand * card_size.x / 2.0f, position.y };
		olc::vf2d increment = { card_size.x, 0.0f };

		for (auto& c : cards) {
			c.position = start_pos;
			start_pos += increment;
		}
	}

	// pos is the "center top position" of the hand
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
	return std::round(std::pow(1.6, x) / 2.236);
}

int Score(const std::vector<Card>& run) {
	int length_score = Fib(run.size());

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

	int number_score = Fib(max_value(number_counts));
	int shape_score = Fib(max_value(shape_counts));
	int letter_score = Fib(max_value(letter_counts));
	int color_score = Fib(max_value(color_counts));

	return length_score + number_score + shape_score + letter_score + color_score;
}

int game_length = 5;
// The players hand
Hand hand;
int score = 0;

// Index into hand.cards for the card just played, for animation
int card_played_index = -1;




enum class GameState {
	NONE,
	START_SCREEN,
	GAME_START, // Start of a game.
	DRAW_CARDS, // Draw cards up to the hand limit
	PICK_CARD, //Pick cards to play
	SCORE_TURN, //calcualte the score of the current play
	END_GAME, //End of the game, show final score
	ANIMATE_PLAY,
	ANIMATE_UNPLAY,
	LENGTH_SELECT,
};


struct State {
	olc::PixelGameEngine* pge;
	explicit State(olc::PixelGameEngine* pge_) : pge(pge_) {};
	virtual ~State() = default;
	virtual void EnterState() {};
	virtual GameState OnUserUpdate(float fElapsedTime) = 0;
	virtual void ExitState() {};
};

struct StartScreenState : public State {
	StartScreenState(olc::PixelGameEngine* pge) : State(pge) {};

	olc::Renderable memory;

	olc::vf2d memory_pos[4] = {
		{32.0f, 64.0f},
		{32.0f, 128.0f},
		{224.0f, 128.0f},
		{224.0f, 64.0f}
	};

	olc::vf2d memory_uv[4] = {
		{0.0f, 0.0f},
		{0.0f, 1.0f},
		{1.0f, 1.0f},
		{1.0f, 0.0f}
	};

	olc::Pixel memory_color[4] = {
		olc::WHITE,
		olc::WHITE,
		olc::BLANK,
		olc::BLANK
	};

	void EnterState() override {
		olc::vf2d text_size = pge->GetTextSize("RUN");

		memory.Create(text_size.x, text_size.y);

		pge->SetDrawTarget(memory.Sprite());
		pge->Clear(olc::BLANK);
		pge->DrawString({ 0,0 }, "RUN");
		memory.Decal()->Update();
		pge->SetDrawTarget((uint8_t)0);

	}

	GameState OnUserUpdate(float fElapsedTime) {
		GameState next_state = GameState::START_SCREEN;

		olc::vf2d button_pos = olc::vf2d{ pge->ScreenWidth() / 3.0f, pge->ScreenHeight() * 2.0f / 3.0f };
		olc::vf2d button_size = olc::vf2d{ pge->ScreenWidth() / 3.0f, pge->ScreenHeight() / 6.0f };

		pge->FillRectDecal(button_pos, button_size, olc::DARK_GREY);

		olc::vf2d text_size = pge->GetTextSize("Start");

		olc::vf2d scale = (button_size) / text_size;

		pge->DrawStringDecal(button_pos + olc::vf2d{ 2.0, 2.0 }, "Start", olc::BLACK, scale);

		olc::Decal* m = memory.Decal();

		pge->DrawExplicitDecal(m, &memory_pos[0], memory_uv, memory_color);

		if (pge->GetMouse(0).bPressed) {
			if (PointInRect(pge->GetMousePos(), button_pos, button_size)) {
				next_state = GameState::LENGTH_SELECT;
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

	GameState OnUserUpdate(float fElapsedTime) {
		//hand.Draw(pge, olc::vf2d{ 128.0f, 205.0f });
		return GameState::DRAW_CARDS;
	}
};

struct DrawCardsState : public State {
	DrawCardsState(olc::PixelGameEngine* pge) : State(pge) {};

	void EnterState() override {

		int cards_to_draw = std::min(hand.max_size - hand.cards.size(), the_deck.size());


		for (int i = 0; i < cards_to_draw; i++) {
			hand.Add(the_deck.back());
			the_deck.pop_back();
		}
	}

	GameState OnUserUpdate(float fElapsedTime) {
		hand.Draw(pge);
		if (hand.cards.size() < 3) {
			return GameState::END_GAME;
		}
		return GameState::PICK_CARD;
	}
};

struct PickCardState : public State {
	PickCardState(olc::PixelGameEngine* pge) : State(pge) {};

	GameState OnUserUpdate(float fElapsedTime) {
		GameState next_state = GameState::PICK_CARD;

		in_play.Draw(pge);
		hand.Draw(pge);

		int cards_in_hand = hand.cards.size();

		olc::vf2d start_pos = { 128.0f - cards_in_hand * card_size.x / 2.0f, 205.0f };
		olc::vf2d increment = { card_size.x, 0.0f };

		if (pge->GetMouse(0).bPressed) {
			for (int i = 0; i < hand.cards.size(); i++) {
				if (PointInRect(pge->GetMousePos(), hand.cards[i].position, card_size)) {
					if (in_play.cards.size() == 0 || IsValid(in_play.cards.back(), hand.cards[i])) {
						
						card_played_index = i;
						next_state = GameState::ANIMATE_PLAY;
						//in_play.Add(hand.cards[i]);
						//hand.cards.erase(hand.cards.begin() + i);
					}
				}
			}
		}

		// If the user clicked on the last in play card, let them take it back
		if (in_play.cards.size() && !in_play.cards.back().locked && pge->GetMouse(0).bPressed) {
			if (PointInRect(pge->GetMousePos(), in_play.cards.back().position, card_size)) {
				//card_played_index = 
				//hand.Add(in_play.cards.back());
				//in_play.cards.pop_back();
				next_state = GameState::ANIMATE_UNPLAY;
			}
		}

		// Draw an end turn button, if a long enough run has been made
		if (in_play.cards.size() > 2) {
			olc::vf2d button_pos = { 2.0f, 193.0f };
			olc::vf2d button_size = { 80.0f, 10.0f };

			pge->FillRectDecal(button_pos, button_size, olc::DARK_GREY);

			olc::vf2d text_size = pge->GetTextSize("End Turn");
			olc::vf2d scale = (button_size) / text_size;

			pge->DrawStringDecal(button_pos + olc::vf2d{ 0.5f, 0.5f }, "End Turn", olc::BLACK, scale);

			if (pge->GetMouse(0).bPressed) {
				if (PointInRect(pge->GetMousePos(), button_pos, button_size)) {
					score += Score(in_play.cards);
					in_play.cards.clear();
					next_state = GameState::DRAW_CARDS;
				}
			}
		}

		// Draw a discard button ending a turn but granting no points and discarding the hand
		{
			olc::vf2d button_pos = { 174.0f, 193.0f };
			olc::vf2d button_size = { 80.0f, 10.0f };

			pge->FillRectDecal(button_pos, button_size, olc::DARK_GREY);

			olc::vf2d text_size = pge->GetTextSize("Discard");
			olc::vf2d scale = (button_size) / text_size;

			pge->DrawStringDecal(button_pos + olc::vf2d{ 0.5f, 0.5f }, "Discard", olc::BLACK, scale);

			if (pge->GetMouse(0).bPressed) {
				if (PointInRect(pge->GetMousePos(), button_pos, button_size)) {
					in_play.cards.clear();
					hand.cards.clear();
					next_state = GameState::DRAW_CARDS;
				}
			}
		}

		pge->DrawStringDecal({ 10.0f, 10.0f }, "Score: " + std::to_string(score));
		pge->DrawStringDecal({ 10.0f, 20.0f }, "Deck : " + std::to_string(the_deck.size()));

		return next_state;
	}
};

struct EndGameState : public State {
	EndGameState(olc::PixelGameEngine* pge) : State(pge) {};

	void EnterState() override {
		hand.cards.clear();
		in_play.cards.clear();
		the_deck.clear();
		
	}

	GameState OnUserUpdate(float fElapsedTime) {
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
		olc::vf2d position = { in_play.position.x - (in_play.cards.size() + 1) * card_size.x / 2.0f, in_play.position.y };
		olc::vf2d increment = { card_size.x, 0.0f };
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
		position = { hand.position.x - (hand.cards.size() - 1) * card_size.x / 2.0f, hand.position.y};
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

	GameState OnUserUpdate(float fElapsedTime) {
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
			return GameState::PICK_CARD;
		}

		in_play.Draw(pge);
		hand.Draw(pge);

		return GameState::ANIMATE_PLAY;
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
		olc::vf2d position = { in_play.position.x - (in_play.cards.size() - 1) * card_size.x / 2.0f, in_play.position.y };
		olc::vf2d increment = { card_size.x, 0.0f };
		for (int i = 0; i < in_play.cards.size(); i++) {
			AnimationState as;
			as.start_pos = in_play.cards[i].position;
			as.end_pos = position;
			as.index = i;
			position += increment;
			play_animation.push_back(as);
		}

		// Figure out where all the cards in hand will be moving to.
		position = { hand.position.x - (hand.cards.size() + 1) * card_size.x / 2.0f, hand.position.y };
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

	GameState OnUserUpdate(float fElapsedTime) {
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
			return GameState::PICK_CARD;
		}

		in_play.Draw(pge);
		hand.Draw(pge);

		return GameState::ANIMATE_UNPLAY;
	}
};

struct LengthSelectState : public State {
	struct Button {
		std::string text;
		olc::vf2d pos;
		olc::vf2d size;
		olc::vf2d text_size;
		int value;
	};

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


	GameState OnUserUpdate(float fElapsedTime) {
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


std::map<GameState, std::unique_ptr<State>> gameStates;


class Example : public olc::PixelGameEngine
{
public:



	Example()
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

		for (int i = 3; i <= 11; i++) {
			shape_primitives[i] = MakePrimitive(i);
		}

		the_deck = CreateDeck(5, 5, 5);

		//

		//for (int i = 0; i < hand.max_size; i++) {
		//	hand.Add(the_deck.back());
		//	the_deck.pop_back();
		//}
		
		return true;
	}

	int side_count = 3;
	int deck_index = 0;

	bool OnUserUpdate(float fElapsedTime) override
	{
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
	Example demo;
	if (demo.Construct(256, 240, 4, 4))
		demo.Start();

	return 0;
}