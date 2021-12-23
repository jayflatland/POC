
PIECES = {
    1: {"f": ("GR", "YR", "PG", "YP"), "b": ("GY", "RY", "PG", "RP")},
    2: {"f": ("RP", "YR", "PG", "YG"), "b": ("YP", "GY", "PR", "GR")},
    3: {"f": ("GY", "RP", "GP", "YR"), "b": ("PG", "YR", "PY", "RG")},
    4: {"f": ("PR", "YP", "RG", "YG"), "b": ("GR", "PG", "RY", "PY")},
    5: {"f": ("RG", "PR", "GY", "PY"), "b": ("YG", "RY", "GP", "RP")},
    6: {"f": ("GP", "RY", "GY", "PR"), "b": ("YP", "GR", "YG", "RP")},
    7: {"f": ("RG", "YP", "RY", "PG"), "b": ("RG", "PY", "RY", "GP")},
    8: {"f": ("PR", "YR", "GP", "YG"), "b": ("PY", "GY", "RP", "GR")},
    9: {"f": ("PY", "GR", "PR", "YG"), "b": ("YP", "RG", "YR", "GP")},
}


def matches(p1, p2, p1_r, p2_r):
    if p1 == 0 or p2 == 0:
        return False

    tile1, fb1, o1 = p1
    tile2, fb2, o2 = p2

    clrs1 = PIECES[tile1][fb1]
    clrs2 = PIECES[tile2][fb2]

    clr1 = clrs1[(o1+p1_r)%4]
    clr2 = clrs2[(o2+p2_r)%4]

    reversed_clr2 = clr2[::-1]  # reverse the clr2

    m = clr1 == reversed_clr2
    # if m:
    #     print(f"matches({p1}, {p2}, {p1_r}, {p2_r})")

    return m


def matches_lr(p1, p2):
    return matches(p1, p2, 1, 3)


def matches_ud(p1, p2):
    return matches(p1, p2, 2, 0)


def count_matches(board):
    cnt = 0
    if matches_lr(board[0], board[1]): cnt += 1
    if matches_lr(board[1], board[2]): cnt += 1

    if matches_lr(board[3], board[4]): cnt += 1
    if matches_lr(board[4], board[5]): cnt += 1

    if matches_lr(board[6], board[7]): cnt += 1
    if matches_lr(board[7], board[8]): cnt += 1

    if matches_ud(board[0], board[3]): cnt += 1
    if matches_ud(board[3], board[6]): cnt += 1

    if matches_ud(board[1], board[4]): cnt += 1
    if matches_ud(board[4], board[7]): cnt += 1

    if matches_ud(board[2], board[5]): cnt += 1
    if matches_ud(board[5], board[8]): cnt += 1

    return cnt


"""
Board array layout
[ 0, 1 , 2,
  3, 4,  5,
  6, 7,  8]

Orient
 0 = normal
 1 = CW 90
 2 = CW 180
 3 = CW 270 (CCW 90)
"""


board = [
    #tile   f/b     orient
    (  1,   "f",      0  ),
    (  2,   "f",      0  ),
    (  3,   "f",      0  ),
    (  4,   "f",      0  ),
    (  5,   "f",      0  ),
    (  6,   "f",      0  ),
    (  7,   "f",      0  ),
    (  8,   "f",      0  ),
    (  9,   "f",      0  ),
]

empty_board = [0, 0, 0, 0, 0, 0, 0, 0, 0]


tiles = {1, 2, 3, 4, 5, 6, 7, 8, 9}
sides = {"f", "b"}
orients = {0, 1, 2, 3}


def find_unplaced_tiles(board):
    placed_tiles = set()
    for p in board:
        if p == 0:
            continue
        placed_tiles.add(p[0])
    return tiles - placed_tiles


def expected_matches_at_filled(next_place_at):
    answers = {
        0:  0,    1:  1,    2:  2,
        3:  3,    4:  5,    5:  7,
        6:  8,    7: 10,    8: 12,
    }
    return answers[next_place_at]


search_cnt = 0
def place_next_tile(board, next_place_at=0):
    global search_cnt
    search_cnt += 1
    unplaced_tiles = find_unplaced_tiles(board)
    newboard = board[:] # make a copy

    target_cnt = expected_matches_at_filled(next_place_at)

    for t in unplaced_tiles:
        for s in sides:
            for o in orients:
                newboard[next_place_at] = (t, s, o)
                cnt = count_matches(newboard)
                if cnt == target_cnt:
                    if next_place_at < 8:
                        place_next_tile(newboard, next_place_at + 1)
                    else:
                        print("FOUND IT!!")
                        print(newboard[0:3])
                        print(newboard[3:6])
                        print(newboard[6:9])

place_next_tile(empty_board)
print(f"Searched {search_cnt} boards")