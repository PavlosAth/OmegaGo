#include "goteam.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  stoneType color;  // 0: Empty, 1: Black, 2: White
  int16_t group_id; // Group ID for chain or region
} interSection;

typedef struct {
  uint8_t status;       // 0: dead  1: black  2: white 3: active(region-temporary state)
  uint16_t *adj_groups; // Dynamic array for adjacent groups
  int num_adj;
  int capacity; // Current allocated size
} Group;

int add_adj(Group *gr, int id) // add the id of a group as an adjacent to the Group gr
{
  for (int i = 0; i < gr->num_adj; i++)
    if (gr->adj_groups[i] == id) return 0; // check if it already exists

  if (gr->num_adj == gr->capacity) { // reallocate memory
    gr->capacity *= 2;
    gr->adj_groups = realloc(gr->adj_groups, gr->capacity * sizeof(uint16_t));
    if (gr->adj_groups == NULL) return 1;
  }
  gr->adj_groups[gr->num_adj++] = id;
  return 0;
}

void run_benson(Group chains[], int c_count, Group regions[], int r_count)
{
  uint8_t changed = 1;
  while (changed) {
    changed = 0;

    // update region status based on adjacency
    for (int r = 0; r < r_count; r++) {
      uint8_t current_status = 3; // temporary

      for (int i = 0; i < regions[r].num_adj; i++) { // loop adjacent chains
        int c_id = regions[r].adj_groups[i];
        if (chains[c_id].status == 0) continue; // Ignore dead chains

        if (current_status == 3) current_status = chains[c_id].status;
        else if (current_status != chains[c_id].status) {
          current_status = 0; // Compromised by two colors
          break;
        }
      }
      if (current_status == 3) current_status = 0;
      if (regions[r].status != current_status) { // changing status
        regions[r].status = current_status;
        changed = 1;
      }
    }

    // kill chains
    for (int c = 0; c < c_count; c++) {
      if (chains[c].status == 0) continue; // skip already dead

      int healthy_eyes = 0; // eyes that are fully enclosed by the chains color
      int potential_eyes = 0;

      for (int i = 0; i < chains[c].num_adj; i++) {
        int r_id = chains[c].adj_groups[i];

        if (regions[r_id].status == chains[c].status) healthy_eyes++; // the region belongs to the chains color
        else potential_eyes++;                                        // otherwise it might be a potential eye
      }

      if (healthy_eyes < 1 || potential_eyes + healthy_eyes < 2) { // if a chain does not have even one eye for itself or cannot form 2 eyes in its space then its dead
        chains[c].status = 0;
        changed = 1;
      }
    }
  }
}

int flood_fill_empty(interSection *b, gameState *game, int start_index, int id, Group *chains)
{
  uint16_t *stack = malloc((game->boardsize + 2) * (game->boardsize + 2) * sizeof(uint16_t)); // stack used for iterative flood fill
  if (stack == NULL) return 1;
  int top = -1;
  stack[++top] = start_index;

  while (top >= 0) {
    uint16_t index = stack[top--];
    if (b[index].color != EMPTY) {
      chains[b[index].group_id].status = b[index].color; // storing the color of the group that was encountered in their group (all chains reach an empty spot, so all are populated)
      if (add_adj(chains + b[index].group_id, id)) {     // adding the current region as an adjacent to that chain
        free(stack);
        return 1;
      }
      continue;
    }
    b[index].group_id = id;
    int8_t offsets[] = {-1, 1, -(game->boardsize + 2), game->boardsize + 2};
    for (int i = 0; i < 4; i++) { // adding adjacent intersections to the stack
      uint16_t near = index + offsets[i];
      if (b[near].color == BORDER) continue;
      if (b[near].color == EMPTY && b[near].group_id != -1) continue;
      stack[++top] = near;
    }
  }
  free(stack);
  return 0;
}

typedef struct {
  float black_score;
  float white_score;
} FinalScore;

FinalScore calculate_final_score(interSection *board, int boardsize, Group chains[], Group regions[], float komi)
{
  FinalScore score = {0.0f, komi}; // Start White with Komi

  for (int i = 1; i <= boardsize; i++) { // loop through every intersections (no borders)
    for (int j = 1; j <= boardsize; j++) {
      int index = j + i * (boardsize + 2); // get index
      int g_id = board[index].group_id;    // group id

      if (board[index].color == EMPTY) { // if territory has settled with no conflict count it for the colors score
        if (regions[g_id].status == 1) score.black_score += 1.0f;
        else if (regions[g_id].status == 2) score.white_score += 1.0f;
      }
      else {
        if (chains[g_id].status != 0) { // the stone is alive
          if (chains[g_id].status == 1) score.black_score += 1.0f;
          else if (chains[g_id].status == 2) score.white_score += 1.0f;
        }
        else { // the stone is dead
          int territory_owner = 0;
          for (int n = 0; n < chains[g_id].num_adj; n++) {
            int r_id = chains[g_id].adj_groups[n];
            if (regions[r_id].status != 0) { // atribute dead stones as territory (connect to adjacent regions)
              territory_owner = regions[r_id].status;
              break;
            }
          }
          if (territory_owner == 1) score.black_score += 1.0f;
          else if (territory_owner == 2) score.white_score += 1.0f;
        }
      }
    }
  }
  return score;
}

int getFinalScore(gameState *game, FinalScore *result)
{
  // initialization
  interSection board[(game->boardsize + 2) * (game->boardsize + 2)]; // temporary board for storing chains and regions
  for (int i = 0; i < (game->boardsize + 2) * (game->boardsize + 2); i++) {
    board[i].color = game->board[i].color; // copying board from gameState
    board[i].group_id = -1;                // initializing
  }

  // find chains
  int c_count = 0;                                                          // number of unique chains
  for (int i = 0; i < (game->boardsize + 2) * (game->boardsize + 2); i++) { // constructing groups for stones (chains)
    if (board[i].color == BORDER || board[i].color == EMPTY || board[i].group_id != -1) continue;

    int16_t currentStone = i; // cycle through the circular structure and assigning the id
    do {
      board[currentStone].group_id = c_count;
    } while ((currentStone = game->board[currentStone].nextStone) != i);
    c_count++;
  }
  // after assigning a unique id to each chain, allocate a group for every id
  Group *Chains = malloc(c_count * sizeof(Group));
  if (Chains == NULL) return 1;
  for (int i = 0; i < c_count; i++) {
    Chains[i].adj_groups = malloc(10 * sizeof(uint16_t)); // adjacent regions, will be assigned in flood fill
    if (Chains[i].adj_groups == NULL) {
      for (int j = 0; j < i; j++) free(Chains[j].adj_groups);
      free(Chains);
      return 1;
    }
    Chains[i].num_adj = 0;
    Chains[i].capacity = 10;
    // color will be assigned in the flood fill used to assign regions
  }

  // find regions
  int r_count = 0; // number of unique regions
  for (int i = 0; i < (game->boardsize + 2) * (game->boardsize + 2); i++) {
    if (board[i].color != EMPTY || board[i].group_id != -1) continue;
    if (flood_fill_empty(board, game, i, r_count++, Chains)) {      // assign a unique id to each region
      for (int j = 0; j < c_count; j++) free(Chains[j].adj_groups); // free memory on error
      free(Chains);
      return 1;
    }
  }

  Group *Regions = malloc(r_count * sizeof(Group)); // after assigning a unique id to each region, allocate a group for every id
  if (Regions == NULL) {                            // freeing
    for (int i = 0; i < c_count; i++) free(Chains[i].adj_groups);
    free(Chains);
    return 1;
  }

  for (int i = 0; i < r_count; i++) {
    Regions[i].adj_groups = malloc(10 * sizeof(uint16_t)); // adjacent chains (will be copied from chains)
    if (Regions[i].adj_groups == NULL) {                   // free memory on error
      for (int j = 0; j < i; j++) free(Regions[j].adj_groups);
      free(Regions);
      for (int j = 0; j < c_count; j++) free(Chains[j].adj_groups);
      free(Chains);
      return 1;
    }
    Regions[i].num_adj = 0;
    Regions[i].capacity = 10;
    Regions[i].status = 3; // temporary status
  }

  for (int i = 0; i < c_count; i++) {
    for (int j = 0; j < Chains[i].num_adj; j++) {
      if (add_adj(Regions + Chains[i].adj_groups[j], i)) { // copying adjacency to the region groups as well
        for (int j = 0; j < r_count; j++) free(Regions[j].adj_groups);
        free(Regions);
        for (int j = 0; j < c_count; j++) free(Chains[j].adj_groups);
        free(Chains);
        return 1;
      }
    }
  }

  run_benson(Chains, c_count, Regions, r_count); // marking dead stones and settling status of regions

  *result = calculate_final_score(board, game->boardsize, Chains, Regions, game->komi);

  for (int j = 0; j < r_count; j++) free(Regions[j].adj_groups); // free memory
  free(Regions);
  for (int j = 0; j < c_count; j++) free(Chains[j].adj_groups);
  free(Chains);
  return 0;
}

int finalScore(gameState *game)
{
  FinalScore result;                       // final result
  if (getFinalScore(game, &result) == 1) { // allocation error
    printf("? failed to allocate memory for final score calculation\n\n");
    return 1;
  }
  if (result.black_score > result.white_score) printf("= B+%.1f\n\n", result.black_score - result.white_score);      // black wins
  else if (result.black_score < result.white_score) printf("= W+%.1f\n\n", result.white_score - result.black_score); // white wins
  else printf("= 0\n\n");                                                                                            // draw
  return 0;
}