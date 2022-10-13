
import time
import random
import numba

codes = ".[]<>+-"

@numba.jit(nopython=True)
def run_program(prog):
    output = []
    mem = [0, 0, 0, 0, 0, 0]
    addr = 0
    pc = 0
    stack = []
    cnt = 0
    while cnt < 1000:
        cnt += 1
        #if pc >= min(25, len(prog)):
        if pc >= len(prog):
            #raise Exception("End of program met without 'e' end instruction")
            break
        c = prog[pc]
        if not c in codes:
            pc += 1
            continue
        #print(f"DEBUG: c={c}, mem={mem}, addr={addr}, pc={pc}, stack={stack}")
        if c == '.':
            output.append(mem[addr])
        elif c == '[':
            stack.append(pc)
        elif c == ']':
            if mem[addr] != 0 and len(stack) > 0:
                #JAY QUESTION - what does real thing do if ']' without a '['?
                #JAY QUESTION - does real thing handle nested loops?  I'm 
                #               assuming yes, but very possibly does not.
                #if len(stack) == 0:
                #    raise Exception("Found ']' without a '[' (empty stack)")
                pc = stack.pop(-1)
                continue
        elif c == '<':
            if addr > 0:
                addr -= 1
        elif c == '>':
            if addr < len(mem) - 1:
                addr += 1
        elif c == '+':
            if mem[addr] < 30:
                mem[addr] += 1
        elif c == '-':
            if mem[addr] > 0:
                mem[addr] -= 1
        elif c == 'e':
            break
        pc += 1
    return output#, mem, addr


#@numba.jit(nopython=True)
def score_output(output, prog):
    score = 0.0

    # Goal COIN - [3, 15, 9, 14]
    if len(output) > 0:
        score += 100 - (output[0] - 3)**2
    if len(output) > 1:
        score += 100 - (output[1] - 15)**2
    if len(output) > 2:
        score += 100 - (output[2] - 9)**2
    if len(output) > 3:
        score += 100 - (output[3] - 14)**2
    # if len(output) > 4:
    #     score -= len(output) - 4
    score -= len(prog) * 0.1
    # score += prog.count("[") * 0.003
    # score += prog.count("]") * 0.003
    # score += prog.count("<")
    # score += prog.count(">")

    return score

#@numba.jit(nopython=True)
def score_program(prog):
    output = run_program(prog)
    return score_output(output, prog)


#@numba.jit(nopython=True)
def breed_programs(parents):
    #This could (should) be a LOT better
    prog = random.choice(parents)

    #unpack because strings are immutable
    prog = list(prog)

    codes = ".[]<>+-"

    #mutation_cnt = random.randint(1, 4) * random.randint(1, 4)
    mutation_cnt = random.randint(1, 200)

    for i in range(mutation_cnt):
        prog_empty = len(prog) == 0
        place = random.randint(0, len(prog) - 1) if not prog_empty else 0
        add_or_remove = random.randint(0, 1)
        if prog_empty or add_or_remove == 0:
            code = random.choice(codes)
            prog.insert(place, code)
        else:
            prog.pop(place)

    #pack back into a string
    prog = "".join(prog)
    return prog
    


#@numba.jit(nopython=True)
if 0:#def run_genetic_algorithm():
    seed_population = ["+++[>+>+++>+++++<<<-]>.>>.<.>-."]
    
    full_population_size = 10
    selected_population_size = 3

    last_report_time = 0.0
    last_report_gen = 0
    generation = 0
    while True:
        generation += 1
        
        #repopulate
        population = set()
        for p in seed_population:
            population.add(p)
        while len(population) < full_population_size:
            parent_a = random.choice(seed_population)
            child = breed_programs([parent_a])
            population.add(child)
    
        #compute scores
        scores = []
        for p in population:
            score = score_program(p)
            scores.append((score, p))
    
        #rank them
        def score_from_tuple(t):
            return t[0]
        scores.sort(key=score_from_tuple, reverse=True)
    
        #thin the herd
        seed_population = []
        top_scores = []
        for t in scores[:selected_population_size]:
            seed_population.append(t[1])
            top_scores.append(t[0])
            
        # report
        now = time.time()
        if now - last_report_time > 1.0:
            dt = now - last_report_time
            d_gens = generation - last_report_gen
            last_report_gen = generation
            gen_rate = d_gens / dt
            test_rate = gen_rate * len(population)
            last_report_time = now
            best_prog = seed_population[0]
            best_output = run_program(best_prog)
            print(f'Gen[{generation: 4d}, {test_rate:.0f}/s] Scores: {top_scores[0]:5.1f} Output: {best_output} Prog: ({len(best_prog)}) "{best_prog}"')

@numba.jit(nopython=True)
def run_exhaustive_search(prog):
    target_output = [3, 15]#, 15, 9, 14]
    winners = []
    if len(prog) < 9:
        for c in codes:
            winners += run_exhaustive_search(prog + c)
    output = run_program(prog)
    if output == target_output:
        winners.append(prog)
    return winners
    #output = score_program("+++[>+>+++>+++++<<<-]>.>>.<.>-.")

winners = run_exhaustive_search("")
print(winners)

#print(run_program("+++[>+>+++>+++++<<<-]>.>>.<.>-."))
#run_genetic_algorithm()
# # %%
# prog = "+++.+++++++."
# #prog = breed_programs(["+++.++++."])
# output = run_program(prog)
# score = score_output(output)
# print(f'"{prog}", {output}, {score}')

#[3, 15, 9, 14]
