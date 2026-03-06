# OmegaGo - goteam:

`goteam` is a program designed to play the game `GO` following the GTP (Go-Text-Protocol).

---

To make and use the program,  
First clone the repository:

```bash
$ git clone https://github.com/progintro/hw3-omegago.git
```

Then run:

```bash
$ make
...
$ ./goteam
```

---

Out of the many GTP commands, goteam supports the following:

|     Command      |                                             Behaviour                                             |
| :--------------: | :-----------------------------------------------------------------------------------------------: |
|       play       |                 Takes as arguments the color, and the index, and places the stone                 |
|     genmove      |                          Using MCTS, generates a valid move on the board                          |
|    boardsize     |             Sets the boardsize in which the game will be played at, supports up to 25             |
|    showboard     |                     Shows the current board state, X is black and O is white                      |
|   clear_board    |                        Resets the current boardstate back to the beginning                        |
|       komi       | Takes a floating point number, in go, white has a disadvantage as such komi is added to his score |
|       name       |                              Prints the name of the engine - OmegaGo                              |
|     version      |                              Shows the current version of the engine                              |
|  time_settings   |                        Tells the engine exactly how much total time it has                        |
|    time_left     |                            Tells the engine how much time it has left                             |
| protocol_version |                           Shows the version of the GTP we are following                           |
|  list_commands   |                              Shows a list of the available commands                               |
|   final_score    |                        Calculates and returns who won, and by what margin                         |
|       quit       |                              Stops the engine and exits the program                               |

## PLAY:

### Usage:

```bash
play <COLOR> A2
```

\<COLOR> should be:

- "Black" or "B"
- "White" or "W"

This command will play the move, if its valid, and calculate its effects on the board.

The calculations are performed through the playMove function that work on the board structure.

### The Board Structure:

Firstly, the board is allocated as a one dimensional array of size $(boardsize + 2)^2$ to also accommodate for the borders of the board.

For every point of the array its color, liberties and the index of the next stone on its chain are kept.

In addition, the liberties form a Disjoint Set Union (DSU) that groups a chain under one representative.<br>
This way, every point of the array stores the index of its representative or the liberties of its chain, if it is itself a representative (as a negative number).

By using this structure, the effect of the played move can be calculated very fast as it becomes just a matter of checking the adjacent chains and tracking liberties for just the representative of each chain.
The chains are connected by a circular structure. Each stone points to the next in its chain, forming a circle.
Thus any chain can be iterated through without the use of a flood fill.

### Change Arrays:

For every one of the 3 different types of data that are stored in the board (color - liberties/DSU - circular structure/Chains), a stack like dynamic array is stored in which every change to the board structure is preserved, giving the engine the ability to rollback the changes that a move made on the board.
To achieve this an additional change array is kept that stores the starting index of each of the other 3 arrays, for each move played (or simulated).

## GENMOVE:

### Usage:

```bash
genmove <COLOR>
```

And it should return

```bash
= D4

```

Whereas D4 has already been placed on the board.

\<COLOR> should be:

- "Black" or "B"
- "White" or "W"

### MCTS Core:

This engine implements a Monte Carlo Tree Search (MCTS) algorithm with RAVE (Rapid Action Value Estimation) and heuristic-guided playouts.

The engine has the following key features:

|             Feature              |                                                               Behaviour                                                                |
| :------------------------------: | :------------------------------------------------------------------------------------------------------------------------------------: |
| (Linear) Arena Memory Management | Uses a pre-allocated NODE_POOL_SIZE (850,000 nodes) to eliminate the overhead of frequent malloc calls during time-sensitive searches. |
|   Heuristic Shape Recognition    |            Detects classic Go shapes like Tiger's Mouth, Empty Triangles, and Hane to bias the search toward "good" moves.             |
|        Tactical Playouts         |                        Simulations are not completely random, it has a tendency to play towards Atari/Captures.                        |
|            Tree Reuse            |                  Retains the relevant subtree after an opponent's move, as to avoid rebuilding the entire tree again.                  |
|           Opening Book           |              MCTS is rather weak during the opening phase of the game, hence we deterministically choose the first moves.              |
|       Progressive widening       |                                    Explore more nodes based on how many visits the parent node has.                                    |
|   Locality bias in simulations   |                            The engine plays 60% of the moves, near the last one, and 40% in a global range.                            |
|     Dynamic time management      |            The engine averages the time per move needed, based on the time_settings command (defualt 60) and the boardsize.            |

Moreover, the engine follows the standard MCTS loop

```fix
Selection -> Expansion -> Simulation -> Backpropagation
```

`Selection:` Navigates the tree using the standard UCB1-AMAF (UCT) formula  
`Expansion:` Adds new legal moves to the tree, the moves are sorted and selected based on several heuristics  
`Simulation:` Performs a rapid "playout" to the end of the game using fastRand and light heuristics.  
`Backpropagation:` Updates the win/loss statistics and RAVE values from the leaf back to the root.

#### Selection:

During the selection phase, the following formula is used:  
$V_i = (1 - \beta) \left( \frac{W_i}{N_i} + C \sqrt{\frac{\ln N_p}{N_i}} \right) + \beta \left( \frac{W_{AMAF}}{N_{AMAF}} \right)$

|        Variable         |                                Description                                |
| :---------------------: | :-----------------------------------------------------------------------: |
|          $V_i$          |   The total selection value, the child with the highest $V_i$ is chosen   |
|      ${W_i}/{N_i}$      |              The winrate of the specific node, (Wins/Visits)              |
|           $C$           |                         The exploration constant.                         |
|          $N_p$          |               The total number of visits of the parent node               |
| ${W_{AMAF}}/{N_{AMAF}}$ | The RAVE Win Rate (All-Moves-As-First) based on global simulation stats.  |
|           $β$           | The Mixing Factor that shifts weight from RAVE to UCT as visits increase. |

---

The formula to find $β$ is:  
$\beta = \sqrt{\frac{K}{3 N_p + K}}$

Where K is the RAVE_CONSTANT (250.0 - 350.0)

- When ${N_p}$ is low, $β$ ≈ 1. The engine utilizes RAVE a lot. It is used to expand the search much faster due to it following this simple logic: If the move worked once, it might as well work again. This in reality is very inefficient, and standard UCT is needed.
- When ${N_p}$​ is high, $β$ ≈ 0. The engine starts utilizing UCT more. This happens because RAVE works more as "intuition", hence when we have the data it's not needed.

Lastly, First-Play-Urgency (FPU) is utilized using the following formula:  
$V_{unvisited} = WinRate_{parent} - FPU_{reduction}$

$FPU_{reduction}$ is a static penalty to prevent the engine from exploring bad moves too early, hence it doesn't treat every unvisited node as equally important.

By setting $FPU_{reduction}$ to 0.25f, the engine assumes that an unexplored move is likely ~25% worse than the current best move.

If a move is marked as "Urgent" (e.g., it's a direct response to an Atari), the $FPU$ is boosted to ensure it is prioritized for exploration.

---

#### Expansion:

The engine only expands a node if it has been visited at least once and doesn't already have children.

This happens as, if we have never visited a node, we must simulate it. If we manage to reach it a second time, it means it's interresting enough to start building a tree with it.

If we expand every move on the very first visit, it would be slow and memory inefficient.

Once the node is expanded, we pick its most promising moves, and sort them (top MOVES_TO_SIMULATE moves). The moves are judged on heavy heuristics, including pattern recognition, eye checks, atari checks, etc. The branching factor of go is too large as to simulate every move.

---

#### Simulation:

During the simulation phase, the engine plays a rapid, psuedo random game against itself. The move generator uses light heuristics as 3000 simulations with lighter heuristics, are better than 1000 with heavy, due to the possible games of GO.

Here is also where the locality bias takes place, where 60% of the simulated moves, are close to the last played move, as to show "fighting"

After a simulation is finally over, it has to determine who won. This is typically done with simple stone counting to avoid making it too heavy.

---

#### Backpropagation:

Iterate backwords through the tree, and update the statistics.  
Every node in the path must get +1 visits, and if the result matches the color of the move, we must increment the win count as well.

The RAVE statistics are also updated. In a Go game, the value of a move often stays the same even if it's played a few turns later. During backpropagation, We look at every move played during the simulation. If a move was played anywhere in the simulation, We update the RAVE stats for the corresponding child of the root.

---

Finally, the engine ends up playing the move with the highest visit count. As it's less noisy than winrate, which is only a rough estimate.

The heuristics used by the engine could have been much heavier, however OmegaGo is mean't to be a blitz engine. It should be able to run within ~60 seconds in a 19x19 board, and still play decently.

### MCTS Sources:
1. [Example MCTS](https://github.com/jSwords91/mcts-c): An example usage of MCTS for connect 4. 
2. [Pachi](https://github.com/pasky/pachi): A strong MCTS based GO engine written in C.

## FINAL SCORE:

The final score of the board position is calculated using a modified version of the Benson's Algorithm and Area Scoring.

Instead of using the pure Benson's Algorithm that marks as dead all chains that do not have at least 2 distinct and healthy eyes, the algorithm treats as dead stones only the chains that do no have even 1 healthy eye to themselves or are unable to form a second potential eye in an area they might share with the enemy.

After the appropriate chains are marked as dead, the score is calculated according to chinese rules.
Every alive stone counts as 1 point and every territory point also counts as 1 point, with white also having the advantage of komi in scoring.
