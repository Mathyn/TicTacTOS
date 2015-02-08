#if !defined(__cplusplus)
#include <stdbool.h> /* C doesn't have booleans by default. */
#endif
#include <stddef.h>
#include <stdint.h>
 
/* Check if the compiler thinks if we are targeting the wrong operating system. */
#if defined(__linux__)
#error "You are not using a cross-compiler, you will most certainly run into trouble"
#endif
 
/* This tutorial will only work for the 32-bit ix86 targets. */
#if !defined(__i386__)
#error "This tutorial needs to be compiled with a ix86-elf compiler"
#endif

enum board_state
{
	UNDECIDED = 0,
	PLAYER1_WIN = 1,
	PLAYER2_WIN = 2,
	DRAW = 3
};
enum board_piece
{
	NONE = 0,
	PLAYER1 = 1,
	PLAYER2 = 2
};
struct Board
{
	uint8_t state;
	uint8_t emptyPieceCount;
	uint8_t pieces[9];
};
struct Move
{
	uint8_t prevBoardXIndex;
	uint8_t prevBoardYIndex;
	uint8_t boardXIndex;
	uint8_t boardYIndex;
	uint8_t pieceXIndex;
	uint8_t pieceYIndex;
	uint8_t piece;
};
struct Game
{
	uint8_t curBoardXIndex;
	uint8_t curBoardYIndex;
	uint8_t curPlayer;
	struct Board boards[9];
};
 
/* Hardware text mode color constants. */
enum vga_color
{
	COLOR_BLACK = 0,
	COLOR_BLUE = 1,
	COLOR_GREEN = 2,
	COLOR_CYAN = 3,
	COLOR_RED = 4,
	COLOR_MAGENTA = 5,
	COLOR_BROWN = 6,
	COLOR_LIGHT_GREY = 7,
	COLOR_DARK_GREY = 8,
	COLOR_LIGHT_BLUE = 9,
	COLOR_LIGHT_GREEN = 10,
	COLOR_LIGHT_CYAN = 11,
	COLOR_LIGHT_RED = 12,
	COLOR_LIGHT_MAGENTA = 13,
	COLOR_LIGHT_BROWN = 14,
	COLOR_WHITE = 15,
};
 
uint8_t make_color(enum vga_color fg, enum vga_color bg)
{
	return fg | bg << 4;
}
 
uint16_t make_vgaentry(char c, uint8_t color)
{
	uint16_t c16 = c;
	uint16_t color16 = color;
	return c16 | color16 << 8;
}
 
size_t strlen(const char* str)
{
	size_t ret = 0;
	while ( str[ret] != 0 )
		ret++;
	return ret;
}

static const size_t VGA_X_OFFSET = 30;

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

static const size_t GAME_BOARD_X_OFFSET = 34; // (80 - 11) / 2 = 69 / 2 = 34
static const size_t GAME_BOARD_Y_OFFSET = 7;  // (25 - 11) / 2 = 14 / 2 = 7

static const size_t MAX_MINMAX_DEPTH = 6;
static const uint32_t MOVE_BUFFER = (uint32_t)(50 * 1024 * 1024); // Skip the first 100MB
static const uint32_t GAME_BUFFER = (uint32_t)(300 * 1024 * 1024); // Skip the first 250MB
static const uint32_t MAX_GAME_BUFFER_SIZE = (uint32_t)(700 * 1024 * 1024);

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer;

size_t moveSizeBytes;
size_t boardSizeBytes;
size_t gameSizeBytes;

uint32_t* move_buffer;
uint32_t* game_buffer;

uint8_t lastPlayerMoveX = 0xFF;
uint8_t lastPlayerMoveY = 0xFF;
uint8_t computerVScomputer = 0;

struct Game game;

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}
static inline void outw(uint16_t port, uint16_t val)
{
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}
static inline void outl(uint16_t port, uint32_t val)
{
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;

    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );

    return ret;
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;

    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );

    return ret;
}
static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;

    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );

    return ret;
}
 
void terminal_initialize()
{
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = make_color(COLOR_LIGHT_GREY, COLOR_BLACK);
	terminal_buffer = (uint16_t*) 0xB8000;
	for ( size_t y = 0; y < VGA_HEIGHT; y++ )
	{
		for ( size_t x = 0; x < VGA_WIDTH; x++ )
		{
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = make_vgaentry(' ', terminal_color);
		}
	}
}
 
void terminal_setcolor(uint8_t color)
{
	terminal_color = color;
}
 
void terminal_putentryat(char c, uint8_t color, size_t x, size_t y)
{
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = make_vgaentry(c, color);
}

void terminal_setcursor(size_t x, size_t y)
{
	unsigned short position = (y * 80) + x;

	// cursor LOW port to vga INDEX register
	outb(0x3D4, 0x0F);
	outb(0x3D5, (unsigned char)(position&0xFF));
	// cursor HIGH port to vga INDEX register
	outb(0x3D4, 0x0E);
	outb(0x3D5, (unsigned char )((position>>8)&0xFF));
}

void terminal_wraplines()
{
	// First move all lines up by one
	for(uint8_t currentRow = 1; currentRow < VGA_HEIGHT; currentRow++)
	{
		uint8_t copyToRow = currentRow - 1;
		
		uint16_t* currentRowMemAddr = (uint16_t*)(terminal_buffer + (currentRow * VGA_WIDTH) + VGA_X_OFFSET);
		uint16_t* copyToRowMemAddr = (uint16_t*)(terminal_buffer + (copyToRow * VGA_WIDTH) + VGA_X_OFFSET);

		for(uint8_t column = 0; column < VGA_WIDTH - VGA_X_OFFSET; column++)
			copyToRowMemAddr[column] = currentRowMemAddr[column];
	}

	// Next clear the current bottom line
	uint16_t* bottomLineMemAddr = (uint16_t*)(terminal_buffer + ((VGA_HEIGHT - 1) * VGA_WIDTH + VGA_X_OFFSET));
	for(uint8_t column = 0; column < VGA_WIDTH - VGA_X_OFFSET; column++)
		bottomLineMemAddr[column] = make_vgaentry(' ', terminal_color);

	// And lastly reset the column counter
	terminal_column = 0;
	terminal_row = VGA_HEIGHT - 1;
}
void terminal_putchar(char c)
{
	terminal_putentryat(c, terminal_color, terminal_column + VGA_X_OFFSET, terminal_row);
	if ( ++terminal_column == VGA_WIDTH )
	{
		terminal_column = VGA_X_OFFSET;
		if ( ++terminal_row == VGA_HEIGHT )
		{
			// Terminal overflow, move all characters one line up
			terminal_wraplines();
		}
	}
}

void terminal_newline()
{
	terminal_row++;
	terminal_column = 0;

	if (terminal_row == VGA_HEIGHT )
	{
		// Terminal overflow, move all characters one line up
		terminal_wraplines();
	}
}
 
void terminal_writestring(const char* data)
{
	size_t datalen = strlen(data);
	for ( size_t i = 0; i < datalen; i++ )
	{
		char c = data[i];
		if(c == '\n')
		{
			terminal_newline();
		}
		else
		{
			terminal_putchar(data[i]);
		}
	}	
}
void terminal_println(const char* data)
{
	terminal_writestring(data);
	terminal_newline();
}
void terminal_print_int(int val)
{
	char result[13];
	result[10] = '\n';
	result[11] = 0;

	int isNegative = val < 0;
	if(isNegative)
		val = -val;

	int curIndex = 9;
	do
	{
		unsigned int part = val % 10;

		result[curIndex--] = '0' + part;

		val /= 10;
	}
	while(val > 0);

	if(isNegative)
		result[curIndex--] = '-';

	terminal_writestring((const char*)&result[curIndex + 1]);
}

void byteToHexString(unsigned char byte, char* result)
{
	unsigned char nibble1 = (byte & 0xF0) >> 4;
	unsigned char nibble2 = byte & 0x0F;

	result[0] = nibble1 <= 9 ? '0' + nibble1 : 'A' - 10 + nibble1;
	result[1] = nibble2 <= 9 ? '0' + nibble2 : 'A' - 10 + nibble2;
}

void reset_gameboard(struct Board* board)
{
	board->state = UNDECIDED;
	board->emptyPieceCount = 9;
	for(uint8_t i = 0; i < 9; i++)
		board->pieces[i] = NONE;
}
void reset_game()
{
	game.curBoardXIndex = 0xFF;
	game.curBoardYIndex = 0xFF;
	game.curPlayer = PLAYER1;
	for(uint8_t i = 0; i < 9; i++)
	{
		reset_gameboard(&game.boards[i]);
	}
}

void draw_gameboard(struct Board* board, uint8_t boardX, uint8_t boardY)
{
	// Fill the screen with a background color
	/*for(int x = 0; x < VGA_WIDTH; x++)
	{
		for(int y = 0; y < VGA_HEIGHT; y++)
		{
			terminal_putentryat(' ', COLOR_MAGENTA << 4, x, y);
		}
	}*/

	// Determine the background color
	uint8_t backgroundColor = COLOR_DARK_GREY << 4;

	if(computerVScomputer || game.curPlayer == PLAYER1)
	{
		if(boardX == game.curBoardXIndex && boardY == game.curBoardYIndex)
			backgroundColor = COLOR_LIGHT_GREY << 4;
		else if(game.curBoardXIndex == 0xFF)
			backgroundColor = COLOR_LIGHT_GREY << 4;
		else if(game.boards[game.curBoardYIndex * 3 + game.curBoardXIndex].state != UNDECIDED)
				backgroundColor = COLOR_LIGHT_GREY << 4;
	}

	if(board->state == PLAYER1_WIN)
		backgroundColor = COLOR_GREEN << 4;
	else if(board->state == PLAYER2_WIN)
		backgroundColor = COLOR_RED << 4;

	for(uint8_t i = 0; i < 9; i++)
	{
		enum board_piece piece = board->pieces[i];

		uint8_t xOffset = i % 3;
		uint8_t yOffset = i / 3;

		uint8_t x = boardX * 4 + xOffset + GAME_BOARD_X_OFFSET;
		uint8_t y = boardY * 4 + yOffset + GAME_BOARD_Y_OFFSET;

		int wasLastMove = (boardX * 3 + xOffset) == lastPlayerMoveX && (boardY * 3 + yOffset) == lastPlayerMoveY;

		switch(piece)
		{
		case(NONE):
			terminal_putentryat('_', COLOR_WHITE | backgroundColor, x, y);
			break;
		case(PLAYER1):
			terminal_putentryat('X', (wasLastMove ? COLOR_LIGHT_BLUE : COLOR_BLUE) | backgroundColor, x, y);
			break;
		case(PLAYER2):
			terminal_putentryat('O', (wasLastMove ? COLOR_LIGHT_BROWN : COLOR_BROWN) | backgroundColor, x, y);
			break;
		}
	}

	uint8_t borderBackgroundColor = COLOR_CYAN + (COLOR_LIGHT_BLUE << 4);

	// Draw an extra border around the game to make it look extra ASCII fancy
	terminal_putentryat('*', borderBackgroundColor, GAME_BOARD_X_OFFSET - 1, GAME_BOARD_Y_OFFSET - 1);
	terminal_putentryat('*', borderBackgroundColor, GAME_BOARD_X_OFFSET + 11, GAME_BOARD_Y_OFFSET - 1);
	terminal_putentryat('*', borderBackgroundColor, GAME_BOARD_X_OFFSET + 11, GAME_BOARD_Y_OFFSET + 11);
	terminal_putentryat('*', borderBackgroundColor, GAME_BOARD_X_OFFSET - 1, GAME_BOARD_Y_OFFSET + 11);

	// Draw the left column
	for(int i = 0; i < 11; i++)
	{
		terminal_putentryat('|', borderBackgroundColor, GAME_BOARD_X_OFFSET - 1, GAME_BOARD_Y_OFFSET + i);
	}
	// Draw the right column
	for(int i = 0; i < 11; i++)
	{
		terminal_putentryat('|', borderBackgroundColor, GAME_BOARD_X_OFFSET + 11, GAME_BOARD_Y_OFFSET + i);
	}
	// Draw the top column
	for(int i = 0; i < 11; i++)
	{
		terminal_putentryat('-', borderBackgroundColor, GAME_BOARD_X_OFFSET + i, GAME_BOARD_Y_OFFSET - 1);
	}
	// Draw the bottom column
	for(int i = 0; i < 11; i++)
	{
		terminal_putentryat('-', borderBackgroundColor, GAME_BOARD_X_OFFSET + i, GAME_BOARD_Y_OFFSET + 11);
	}

	// Draw gray lines between each sub board
	for(int y = 0; y < 2; y++)
	{
		int yOffset = (y + 1) * 3 + y;
		for(int x = 0; x < 11; x++)
		{
			terminal_putentryat(' ', borderBackgroundColor, GAME_BOARD_X_OFFSET + x, GAME_BOARD_Y_OFFSET + yOffset);

			terminal_putentryat(' ', borderBackgroundColor, GAME_BOARD_X_OFFSET + yOffset, GAME_BOARD_Y_OFFSET + x);
		}
	}
}
void draw_game()
{
	for(uint8_t i = 0; i < 9; i++)
	{
		int boardXIndex = i % 3;
		int boardYIndex = i / 3;

		draw_gameboard(&game.boards[i], boardXIndex, boardYIndex);
	}

	/*for(int x = 0; x < 3; x++)
	{
		for(int y = 0; y < 3; y++)
		{
			int index = y * 3 + x;

			struct Board* board = &game.boards[index];
			terminal_putentryat('0' + board->state, COLOR_WHITE, x, y + 12);
		}
	}*/
}

void put_moves_for_board(struct Board* board, int boardX, int boardY, enum board_piece player)
{
	if(board->state != UNDECIDED || board->emptyPieceCount == 0)
		return;

	// We generate moves by iterating over every position where a move could
	// be made. Next we check if the position is empty (NONE), if it is we add the
	// position as a move.
	for(int y = 0; y < 3; y++)
	{
		for(int x = 0; x < 3; x++)
		{
			int index = y * 3 + x;

			if(board->pieces[index] == NONE)
			{
				// We found a place where we can place a piece
				struct Move* move = (struct Move*)move_buffer;
				move->pieceXIndex = x;
				move->pieceYIndex = y;
				move->boardXIndex = boardX;
				move->boardYIndex = boardY;
				move->piece = player;

				move_buffer += moveSizeBytes;
			}
		}
	}
}
uint32_t* put_moves_for_game(struct Game* game)
{
	uint32_t* startAddr = move_buffer;

	// Is a game board already selected to play on?
	if(game->curBoardXIndex == 0xFF)
	{
		// First move, select moves from all boards
		for(int i = 0; i < 9; i++)
		{
			put_moves_for_board(&game->boards[i], i % 3, i / 3, game->curPlayer);
		}
	}
	else
	{
		// A game board is selected, but it might be that the game board
		// has already been resolved (win, loss, draw).
		uint8_t boardIndex = game->curBoardYIndex * 3 + game->curBoardXIndex;

		struct Board* board = &game->boards[boardIndex];
		if(board->state == UNDECIDED && board->emptyPieceCount > 0)
		{
			// The game is undecided, we only need to add the moves for this board
			put_moves_for_board(board, game->curBoardXIndex, game->curBoardYIndex, game->curPlayer);
		}
		else
		{
			// The current board has already been resolved, we must add
			// the moves of all other boards instead.
			for(int i = 0; i < 9; i++)
			{
				put_moves_for_board(&game->boards[i], i % 3, i / 3, game->curPlayer);
			}
		}
	}

	if(move_buffer >= GAME_BUFFER)
		terminal_println("MOVE BUFFER OVERFLOW");

	return startAddr;
}
enum board_piece get_next_player(enum board_piece player)
{
	return player == PLAYER1 ? PLAYER2 : PLAYER1;
}
void update_board_state(struct Board* board)
{
	// Check horizontal lines for a win situation
	for(int y = 0; y < 3; y++)
	{
		int startIndex = y * 3;
		if(board->pieces[startIndex] != NONE &&
		   board->pieces[startIndex] == board->pieces[startIndex + 1] &&
		   board->pieces[startIndex + 1] == board->pieces[startIndex + 2])
		{
			// We have a winner!
			board->state = board->pieces[startIndex];
			return;
		}
	}

	// Check vertical lines for a win situation
	for(int x = 0; x < 3; x++)
	{
		if(board->pieces[x] != NONE &&
		   board->pieces[x] == board->pieces[x + 3] &&
		   board->pieces[x + 3] == board->pieces[x + 6])
		{
			// We have a winner!
			board->state = board->pieces[x];
			return;
		}
	}

	// Check diagonal lines for a win situation
	if(board->pieces[0] != NONE &&
	   board->pieces[0] == board->pieces[4] &&
	   board->pieces[4] == board->pieces[8])
	{
		// We have a winner!
		board->state = board->pieces[0];
		return;
	}

	if(board->pieces[2] != NONE &&
	   board->pieces[2] == board->pieces[4] &&
	   board->pieces[4] == board->pieces[6])
	{
		// We have a winner!
		board->state = board->pieces[2];
		return;
	}

	// If we could not find a winning state and there are no empty
	// places left the game state has become a draw.
	if(board->emptyPieceCount == 0)
		board->state = DRAW;
	else
		board->state = UNDECIDED;
}

int is_valid_move(struct Game* game, struct Move* move)
{
	// Check if the player is allowed to do a move
	if(game->curPlayer != move->piece)
		return 0;

	int boardIndex = move->boardYIndex * 3 + move->boardXIndex;
	int pieceIndex = move->pieceYIndex * 3 + move->pieceXIndex;

	// Check if the board is not already resolved (win, loss, draw)
	struct Board* board = &game->boards[boardIndex];
	if(board->state != UNDECIDED)
		return 0;

	// Check if the position is not already taken
	if(board->pieces[pieceIndex] != NONE)
		return 0;

	// Check if a move can be made in the current board. A move can be made if:
	// - The game has not yet selected a board to play on
	// - The move board X and Y index are equal to the game current board X and Y index
	if(game->curBoardXIndex != 0xFF)
	{
		enum board_state state = game->boards[game->curBoardYIndex * 3 + game->curBoardXIndex].state;

		if(state == UNDECIDED && (game->curBoardXIndex != move->boardXIndex || game->curBoardYIndex != move->boardYIndex))
			return 0;
	}

	return 1;
}

void do_move(struct Game* game, struct Move* move)
{
	// Do the move
	int boardIndex = move->boardYIndex * 3 + move->boardXIndex;
	int pieceIndex = move->pieceYIndex * 3 + move->pieceXIndex;

	struct Board* board = &game->boards[boardIndex];

	move->prevBoardXIndex = game->curBoardXIndex;
	move->prevBoardYIndex = game->curBoardYIndex;

	board->pieces[pieceIndex] = move->piece;
	board->emptyPieceCount--;
	game->curBoardXIndex = move->pieceXIndex;
	game->curBoardYIndex = move->pieceYIndex;
	game->curPlayer = get_next_player(game->curPlayer);

	update_board_state(board);
}
void undo_move(struct Game* game, struct Move* move)
{
	// Do the move
	int boardIndex = move->boardYIndex * 3 + move->boardXIndex;
	int pieceIndex = move->pieceYIndex * 3 + move->pieceXIndex;

	struct Board* board = &game->boards[boardIndex];

	board->pieces[pieceIndex] = NONE;
	board->emptyPieceCount++;
	game->curBoardXIndex = move->prevBoardXIndex;
	game->curBoardYIndex = move->prevBoardYIndex;
	game->curPlayer = get_next_player(game->curPlayer);

	// When undoing a move we can just reset the board state to UNDECIDED
	// because no matter what, undoing a move can never result in a win or draw state.
	board->state = UNDECIDED;
}
struct Game* copy_game(struct Game* game)
{
	struct Game* copiedGame = (struct Game*)game_buffer;
	game_buffer += gameSizeBytes;

	if(game_buffer >= MAX_GAME_BUFFER_SIZE)
		terminal_println("GAME BUFFER OVERFLOW");

	uint8_t* copyFromPtr = (uint8_t*)game;
	uint8_t* copyToPtr = (uint8_t*)copiedGame;

	// Copy game to copiedGame
	for(size_t i = 0; i < gameSizeBytes; i++)
	{
		copyToPtr[i] = copyFromPtr[i];
	}

	return copiedGame;
}
struct Game* copy_game_and_do_move(struct Game* game, struct Move* move)
{
	struct Game* copiedGame = copy_game(game);

	do_move(copiedGame, move);

	return copiedGame;
}

enum board_piece get_winning_player(struct Game* game)
{
	// Check the rows
	uint8_t row1 = game->boards[0].state * game->boards[1].state * game->boards[2].state;
	if(row1 == 1)
		return PLAYER1_WIN;
	else if(row1 == 8)
		return PLAYER2_WIN;

	uint8_t row2 = game->boards[3].state * game->boards[4].state * game->boards[5].state;
	if(row2 == 1)
		return PLAYER1_WIN;
	else if(row2 == 8)
		return PLAYER2_WIN;

	uint8_t row3 = game->boards[6].state * game->boards[7].state * game->boards[8].state;
	if(row3 == 1)
		return PLAYER1_WIN;
	else if(row3 == 8)
		return PLAYER2_WIN;

	// Check the columns
	uint8_t col1 = game->boards[0].state * game->boards[3].state * game->boards[6].state;
	if(col1 == 1)
		return PLAYER1_WIN;
	else if(col1 == 8)
		return PLAYER2_WIN;

	uint8_t col2 = game->boards[1].state * game->boards[4].state * game->boards[7].state;
	if(col2 == 1)
		return PLAYER1_WIN;
	else if(col2 == 8)
		return PLAYER2_WIN;

	uint8_t col3 = game->boards[2].state * game->boards[5].state * game->boards[8].state;
	if(col3 == 1)
		return PLAYER1_WIN;
	else if(col3 == 8)
		return PLAYER2_WIN;

	// Check the diagonals
	uint8_t diag1 = game->boards[0].state * game->boards[4].state * game->boards[8].state;
	if(diag1 == 1)
		return PLAYER1_WIN;
	else if(diag1 == 8)
		return PLAYER2_WIN;

	uint8_t diag2 = game->boards[2].state * game->boards[4].state * game->boards[6].state;
	if(diag2 == 1)
		return PLAYER1_WIN;
	else if(diag2 == 8)
		return PLAYER2_WIN;

	for(int i = 0; i < 9; i++)
	{
		if(game->boards[i].state == UNDECIDED)
			return UNDECIDED;
	}

	return DRAW;
}

int score_fill_count(int countP1, int countP2, int baseScore)
{
	if(countP1 > 0 && countP2 > 0)
		return 0;
	else if(countP1 > 0)
	{
		if(countP1 == 1)
			return baseScore;
		else if(countP1 == 2)
			return baseScore * 10;
	}
	else if(countP2 > 0)
	{
		if(countP2 == 1)
			return -baseScore;
		else if(countP2 == 2)
			return -baseScore * 10;
	}

	return 0;
}
int evaluate_board_for_player(struct Board* board, enum board_piece playerToEvaluate)
{
	if(board->state != UNDECIDED && board->state != DRAW)
	{
		// Board is already decided
		return board->state == playerToEvaluate ? 1000 : -1000;
	}

	// Count how close either player is to winning this board
	int totalScore = 0;
	int thisPlayerCount = 0;
	int otherPlayerCount = 0;

	// Check each row
	for(int x = 0; x < 3; x++)
	{
		thisPlayerCount = 0;
		otherPlayerCount = 0;

		for(int y = 0; y < 3; y++)
		{
			enum board_piece piece = board->pieces[y * 3 + x];
			if(piece == playerToEvaluate)
				thisPlayerCount++;
			else if(piece != NONE)
				otherPlayerCount++;
		}

		totalScore += score_fill_count(thisPlayerCount, otherPlayerCount, 10);
	}

	// Check each column
	for(int y = 0; y < 3; y++)
	{
		thisPlayerCount = 0;
		otherPlayerCount = 0;

		for(int x = 0; x < 3; x++)
		{
			enum board_piece piece = board->pieces[y * 3 + x];
			if(piece == playerToEvaluate)
				thisPlayerCount++;
			else if(piece != NONE)
				otherPlayerCount++;
		}

		totalScore += score_fill_count(thisPlayerCount, otherPlayerCount, 10);
	}

	// Check the two diagonals
	thisPlayerCount = 0;
	otherPlayerCount = 0;
	for(int i = 0; i < 3; i++)
	{
		enum board_piece piece = board->pieces[i * 3 + i];
		if(piece == playerToEvaluate)
			thisPlayerCount++;
		else if(piece != NONE)
			otherPlayerCount++;
	}
	totalScore += score_fill_count(thisPlayerCount, otherPlayerCount, 10);

	thisPlayerCount = 0;
	otherPlayerCount = 0;
	for(int i = 2; i >= 0; i--)
	{
		enum board_piece piece = board->pieces[i * 3 + i];
		if(piece == playerToEvaluate)
			thisPlayerCount++;
		else if(piece != NONE)
			otherPlayerCount++;
	}
	totalScore += score_fill_count(thisPlayerCount, otherPlayerCount, 10);

	return totalScore;
}
int evaluate_game_for_player(struct Game* game, enum board_piece playerToEvaluate)
{
	enum board_piece winningPlayer = get_winning_player(game);
	if(winningPlayer == playerToEvaluate)
		return 1000000;
	else if(winningPlayer == DRAW)
		return 0;
	else if(winningPlayer != UNDECIDED)
		return -1000000;

	int totalScore = 0;

	// Evaluate each individual board
	for(int i = 0; i < 9; i++)
	{
		totalScore += evaluate_board_for_player(&game->boards[i], playerToEvaluate);
	}

	// Evaluate the boards as one group
	// Count how close either player is to winning this game
	int thisPlayerCount = 0;
	int otherPlayerCount = 0;

	// Check each row
	for(int x = 0; x < 3; x++)
	{
		thisPlayerCount = 0;
		otherPlayerCount = 0;

		for(int y = 0; y < 3; y++)
		{
			enum board_state state = game->boards[y * 3 + x].state;
			if(state == playerToEvaluate)
				thisPlayerCount++;
			else if(state != UNDECIDED && state != DRAW)
				otherPlayerCount++;
		}

		totalScore += score_fill_count(thisPlayerCount, otherPlayerCount, 100);
	}

	// Check each column
	for(int y = 0; y < 3; y++)
	{
		thisPlayerCount = 0;
		otherPlayerCount = 0;

		for(int x = 0; x < 3; x++)
		{
			enum board_state state = game->boards[y * 3 + x].state;
			if(state == playerToEvaluate)
				thisPlayerCount++;
			else if(state != UNDECIDED && state != DRAW)
				otherPlayerCount++;
		}

		totalScore += score_fill_count(thisPlayerCount, otherPlayerCount, 100);
	}

	// Check the two diagonals
	thisPlayerCount = 0;
	otherPlayerCount = 0;
	for(int i = 0; i < 3; i++)
	{
		enum board_state state = game->boards[i * 3 + i].state;
		if(state == playerToEvaluate)
			thisPlayerCount++;
		else if(state != UNDECIDED && state != DRAW)
			otherPlayerCount++;
	}
	totalScore += score_fill_count(thisPlayerCount, otherPlayerCount, 100);

	thisPlayerCount = 0;
	otherPlayerCount = 0;
	for(int i = 2; i >= 0; i--)
	{
		enum board_state state = game->boards[i * 3 + i].state;
		if(state == playerToEvaluate)
			thisPlayerCount++;
		else if(state != UNDECIDED && state != DRAW)
			otherPlayerCount++;
	}
	totalScore += score_fill_count(thisPlayerCount, otherPlayerCount, 100);

	return totalScore;
}

unsigned int totalCalls = 0;
unsigned int totalCallsInGame = 0;
int do_min_max_rec(struct Game* game, int depth, enum board_piece playerToDoMove, int alpha, int beta)
{
	totalCalls++;

	if(depth == 0)
	{
		// Max depth reached, return the score for the given game for the player who ultimately is going to do a move
		return evaluate_game_for_player(game, playerToDoMove);
	}

	enum board_piece winningPlayer = get_winning_player(game);
	if(winningPlayer == playerToDoMove)
		return 1000000 * (depth + 1);
	else if(winningPlayer == DRAW)
		return 0;
	else if(winningPlayer != UNDECIDED)
		return -1000000 * (depth + 1);

	uint32_t* baseMoveBuffer = move_buffer;
	uint32_t* baseGameBuffer = game_buffer;

	// This is not the last depth, generate a new set of moves
	uint32_t* firstMovePtr = put_moves_for_game(game);

	unsigned int movesGenerated = (move_buffer - firstMovePtr) / moveSizeBytes;
	if(movesGenerated == 0)
	{
		return evaluate_game_for_player(game, playerToDoMove);
	}

	int bestScore = game->curPlayer == playerToDoMove ? -1000000000 : 1000000000;

	for(unsigned int i = 0; i < movesGenerated; i++)
	{
		struct Move* move = (struct Move*)(firstMovePtr + (i * moveSizeBytes));

		struct Game* copiedGame = copy_game_and_do_move(game, move);

		//do_move(game, move);
		int score = do_min_max_rec(copiedGame, depth - 1, playerToDoMove, alpha, beta);
		//undo_move(game, move);

		if(game->curPlayer == playerToDoMove)
		{
			// Try and maximize the score
			if(score > bestScore)
				bestScore = score;

			if(score > alpha)
				alpha = score;
		}
		else
		{
			// Try and minimize the score
			if(score < bestScore)
				bestScore = score;

			if(score < beta)
				beta = score;
		}

		// Check if we can prune this tree
		if(beta <= alpha)
			break;
	}

	// Reset game and move buffer to where it was at the start of this function.
	// This effectively recycles used memory.
	move_buffer = baseMoveBuffer;
	game_buffer = baseGameBuffer;

	return bestScore;
}
void do_mini_max()
{
	move_buffer = MOVE_BUFFER;
	game_buffer = GAME_BUFFER;
	totalCalls = 0;

	// Generate the first set of moves
	uint32_t* firstMovePtr = put_moves_for_game(&game);

	unsigned int movesGenerated = (move_buffer - firstMovePtr) / moveSizeBytes;

	int maxScore = -1000000000;
	struct Move* maxScoreMove;

	// For every move create a new board and recursively do mini max
	for(unsigned int i = 0; i < movesGenerated; i++)
	{
		struct Move* move = (struct Move*)(firstMovePtr + (i * moveSizeBytes));

		struct Game* copiedGame = copy_game_and_do_move(&game, move);

		//do_move(&game, move);
		int score = do_min_max_rec(copiedGame, MAX_MINMAX_DEPTH - 1, game.curPlayer, maxScore, 1000000000);
		//undo_move(&game, move);

		if(score > maxScore)
		{
			// We found a new highest scoring move
			maxScore = score;
			maxScoreMove = move;
		}
	}

	/*terminal_println("---- Best Move ----");
	terminal_print_int(maxScoreMove->boardXIndex);
	terminal_print_int(maxScoreMove->boardYIndex);
	terminal_print_int(maxScoreMove->piece);
	terminal_print_int(maxScoreMove->pieceXIndex);
	terminal_print_int(maxScoreMove->pieceYIndex);*/

	//terminal_println("---- Best Move Score ----");
	//terminal_print_int(maxScore);
	//terminal_print_int(totalCalls);

	totalCallsInGame += totalCalls;

	// Do the best scoring move.
	do_move(&game, maxScoreMove);

	// Store the last made move position. This is used when drawing the game board
	// to give the last made move piece a slightly lighter color.
	lastPlayerMoveX = maxScoreMove->boardXIndex * 3 + maxScoreMove->pieceXIndex;
	lastPlayerMoveY = maxScoreMove->boardYIndex * 3 + maxScoreMove->pieceYIndex;
}
 
#if defined(__cplusplus)
extern "C" /* Use C linkage for kernel_main. */
#endif
void kernel_main()
{
	terminal_initialize();

	// Store the size of the various structs
	// for easy access later.
	struct Move dummyMove;
	struct Board dummyBoard;
	struct Game dummyGame;

	moveSizeBytes = sizeof(dummyMove);
	boardSizeBytes = sizeof(dummyBoard);
	gameSizeBytes = sizeof(dummyGame);

	/*game_buffer = GAME_BUFFER;

	struct Game testGame1;
	testGame1.boards[2].emptyPieceCount = 5;

	struct Game* copiedTestGame1 = copy_game(&testGame1);

	terminal_print_int(testGame1.boards[2].emptyPieceCount);
	terminal_print_int(copiedTestGame1->boards[2].emptyPieceCount);

	terminal_println("---- Test 2 ----");

	uint8_t* ptr = (uint8_t*)(900 * 1024 * 1024);
	ptr[0] = 128;

	terminal_print_int(ptr[0]);

	return;*/

	char hexStr[] = "000";

	// Set keyboard scan code to 2
	outb(0x60, 0xF0);
	outb(0x60, 2);
	uint8_t result = inb(0x60);
	byteToHexString(result, hexStr);
	//terminal_println(hexStr);

	int cursorX = 0;
	int cursorY = 0;
	terminal_setcursor(cursorX, cursorY);

	outb(0x60, 0xF0);
	outb(0x60, 0);
	result = inb(0x60);
	if(result == 0xFA || result == 0xFE)
	{
		outb(0x60, 0);
		result = inb(0x60);
		if(result != 2)
		{
			//terminal_println("Failed to set scancode of keyboard");
			//byteToHexString(result, hexStr);
			//terminal_println(hexStr);
		}
	}
	//else
	//	terminal_println("Failed to get scancode of keyboard");

	//terminal_print_int(moveSizeBytes);
	//terminal_print_int(boardSizeBytes);
	//terminal_print_int(gameSizeBytes);

	reset_game();

	draw_game();

	int enterPressed = 0;
	int leftPressed = 0;
	int rightPressed = 0;
	int upPressed = 0;
	int downPressed = 0;

	int gameResolved = 0;
	while(1)
	{
		unsigned char key = inb(0x60);

		// Check if a valid key was pressed
		switch(key)
		{
		case(0x48):
			if(upPressed || computerVScomputer)
				break;

			upPressed = 1;
			if(cursorY >= 1)
			{
				cursorY--;
				if(cursorY == 3)
					cursorY = 2;
				else if(cursorY == 7)
					cursorY = 6;
			}
			break;
		case(0xC8):
			upPressed = 0;
			break;
		case(0x4D):
			if(rightPressed || computerVScomputer)
				break;

			rightPressed = 1;
			if(cursorX < 10)
			{
				cursorX++;
				if(cursorX == 3)
					cursorX = 4;
				else if(cursorX == 7)
					cursorX = 8;
			}
			break;
		case(0xCD):
			rightPressed = 0;
			break;
		case(0x50):
			if(downPressed || computerVScomputer)
				break;

			downPressed = 1;
			if(cursorY < 10)
			{
				cursorY++;
				if(cursorY == 3)
					cursorY = 4;
				else if(cursorY == 7)
					cursorY = 8;
			}
			break;
		case(0xD0):
			downPressed = 0;
			break;
		case(0x4B):
			if(leftPressed || computerVScomputer)
				break;

			leftPressed = 1;
			if(cursorX >= 1)
			{
				cursorX--;
				if(cursorX == 3)
					cursorX = 2;
				else if(cursorX == 7)
					cursorX = 6;
			}
			break;
		case(0xCB):
			leftPressed = 0;
			break;
		case(0x1C):
			// Enter key down
			if(enterPressed == 1 || gameResolved == 1)
				break;

			enterPressed = 1;

			if(computerVScomputer)
			{
				do_mini_max();
				draw_game();

				enum board_piece winningPlayer = get_winning_player(&game);
				if(winningPlayer != UNDECIDED)
				{
					if(winningPlayer == PLAYER1)
						terminal_println("Player 'X' has won");
					else if(winningPlayer == PLAYER2)
						terminal_println("Player 'O' has won!");
					else
						terminal_println("It's a draw!");
					//terminal_print_int(totalCallsInGame);
					gameResolved = 1;
				}
			}
			else
			{
				if(game.curPlayer == PLAYER1)
				{
					struct Move move;
					move.boardXIndex = cursorX / 4;
					move.boardYIndex = cursorY / 4;
					move.piece = PLAYER1;
					move.pieceXIndex = cursorX % 4;
					move.pieceYIndex = cursorY % 4;

					if(is_valid_move(&game, &move))
					{
						do_move(&game, &move);

						lastPlayerMoveX = move.boardXIndex * 3 + move.pieceXIndex;
						lastPlayerMoveY = move.boardYIndex * 3 + move.pieceYIndex;

						draw_game();
					}
					else
						break;
				}

				enum board_piece winningPlayer = get_winning_player(&game);
				if(winningPlayer != UNDECIDED)
				{
					if(winningPlayer == PLAYER1)
						terminal_println("Player 'X' has won");
					else if(winningPlayer == PLAYER2)
						terminal_println("Player 'O' has won!");
					else
						terminal_println("It's a draw!");
					//terminal_print_int(totalCallsInGame);
					gameResolved = 1;
				}
				else
				{
					do_mini_max();
					draw_game();

					enum board_piece winningPlayer = get_winning_player(&game);
					if(winningPlayer != UNDECIDED)
					{
						if(winningPlayer == PLAYER1)
							terminal_println("Player 'X' has won");
						else if(winningPlayer == PLAYER2)
							terminal_println("Player 'O' has won!");
						else
							terminal_println("It's a draw!");
						//terminal_print_int(totalCallsInGame);
						gameResolved = 1;
					}
				}
			}
			break;
		case(0x9C):
			// Enter key up
			enterPressed = 0;
			break;
		default:
			byteToHexString(key, hexStr);

			//terminal_println(hexStr);
			break;
		}

		terminal_setcursor(cursorX + GAME_BOARD_X_OFFSET, cursorY + GAME_BOARD_Y_OFFSET);
	}
}












