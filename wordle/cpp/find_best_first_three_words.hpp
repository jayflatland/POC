#pragma once

#include <iostream>
#include <unordered_map>
#include <chrono>
#include <fmt/format.h>

#include "pair_hash.hpp"
#include "word.hpp"
#include "word_list.hpp"
#include "mask.hpp"
#include "match_result.hpp"
#include "util.hpp"

using test_type = std::vector<word_type>;

// struct test_type
// {
//     word_type w1_, w2_, w3_;
// };

template <typename StreamT>
StreamT &operator<<(StreamT &os, const test_type &x)
{
    os << "{";
    bool first = true;
    for (auto &&w : x)
    {
        if (!first)
        {
            os << ",";
        }
        first = false;
        // os << '"' << w << '"';
        os << w;
    }
    os << "}";
    return os;
}

inline int color_to_int(const color_type &color)
{
    switch (color)
    {
    default:
    case color_type::gray:
        return 0;
    case color_type::yellow:
        return 1;
    case color_type::green:
        return 2;
    }
    return 0;
}

struct packed_test_result_type// : uint64_t
{
public:
    packed_test_result_type() : x_(0) {}
    packed_test_result_type(const uint64_t &x)
        : x_(x)
    {
    }
    
    static constexpr uint64_t digit_mult() { return 3; }
    static constexpr uint64_t word_mult() { return 243; } // 3**5

    packed_test_result_type operator*(const packed_test_result_type& rhs) const
    {
        return packed_test_result_type(x_ * rhs.x_);
    }

    packed_test_result_type operator+(const packed_test_result_type& rhs) const
    {
        return packed_test_result_type(x_ + rhs.x_);
    }
    
    bool operator==(const packed_test_result_type& rhs) const
    {
        return x_ == rhs.x_;
    }

    // //easier debug
    // static const int digit_mult = 10;
    // static const int word_mult = 100000;
    
    template <typename StreamT>
    friend StreamT& operator<<(StreamT& os, const packed_test_result_type& rhs)
    {
        std::string s;
        uint64_t x = rhs.x_;
        if(x == 0) { s = "0"; }
        while(x > 0)
        {
            //os << (x%digit_mult());
            s = std::to_string(x%digit_mult()) + s;
            x /= digit_mult();
        }
        os << s;
        return os;
    }
    
    const auto& val() const { return x_; }
private:
    uint64_t x_;
};


template<>
struct std::hash<packed_test_result_type>
{
    std::size_t operator()(const packed_test_result_type& x) const
    {
        return std::hash<uint64_t>{}(x.val());
    }
};

// using packed_test_result_type = uint64_t;//not sure i can pack 3 words into u32

// using packed_test_result_type = uint64_t;
// static const packed_test_result_type packed_test_result_digit_mult = 10;
// static const packed_test_result_type packed_test_result_word_mult = 100000;

inline packed_test_result_type match_result_to_int(const match_results_type &m)
{
    packed_test_result_type r;
    for (auto &&lc : m.pos_)
    {
        auto &&c = lc.color_;
        r = r * packed_test_result_type::digit_mult() + color_to_int(c);
    }
    return r;
}

inline packed_test_result_type word_solution_to_int(const word_type &solution, const word_type &word)
{
    return match_result_to_int(calc_match_results(solution, word));
}

inline packed_test_result_type calc_test_result(const test_type &tst, const word_type &solution)
{
    packed_test_result_type r;
    for (auto &&w : tst)
    {
        r = r * packed_test_result_type::word_mult() + word_solution_to_int(solution, w);
    }
    return r;
}

class score_type
{
public:
    double score() const
    {
        return -100.0 * max_ - avg();
        // return cnt_;
        // return -avg();
    }

    int max_ = 0;
    int total_ = 0;
    int cnt_ = 0;
    packed_test_result_type mr_;

    double avg() const
    {
        return (double)total_ / cnt_;
    }

    bool operator<(const score_type &rhs) const { return score() < rhs.score(); }
    bool operator<=(const score_type &rhs) const { return score() <= rhs.score(); }
};

template <typename StreamT>
StreamT &operator<<(StreamT &os, const score_type &x)
{
    os << "(cnt=" << x.cnt_ << ", max=" << x.max_ << ", avg=" << x.avg() << ", score=" << x.score() << ", mr=" << x.mr_ << ")";
    return os;
}

inline score_type calc_score_for_test(const word_list_type &words, const test_type &test)
{
    std::unordered_map<packed_test_result_type, int> counts;
    for (auto &&s : words)
    {
        packed_test_result_type r = calc_test_result(test, s);
        counts[r]++;
    }

    score_type s;
    for (auto &&kv : counts)
    {
        auto &&cnt = kv.second;
        s.total_ += cnt;
        s.max_ = std::max(s.max_, cnt);
        s.cnt_++;
        s.mr_ = kv.first;
    }
    return s;
}

inline test_type random_mutate_test(const word_list_type &words, const test_type &parent)
{
    int wn = random() % parent.size();
    int wi = random() % words.size();

    auto w = words[wi];
    test_type child = parent;
    child[wn] = w;
    return child;
}

inline test_type cycle_mutate_test(const word_list_type &words, const test_type &parent)
{
    static int wi = 0;
    static int wn = 0;
    wn++;
    if (wn >= 3)
    {
        wi++;
        wn = 0;
    }
    if (wi >= words.size())
    {
        wi = 0;
    }

    auto w = words[wi];
    test_type child = parent;
    child[wn] = w;
    return child;
}

inline void find_best_first_three_words()
{
    /*=========================================================================
    Thinking again.
    Each set of 3 words is a test
    Each solution for a given test will result in a 3x match result.
    Each match result has 3**5=243 possibilities
    Each 3x match result has 3**15=14,348,907 possibilities
    =========================================================================*/

    auto words = read_5_letter_words("wordle_words.txt");
    // auto words = read_5_letter_words("wordle_solutions.txt");
    auto solutions = read_5_letter_words("wordle_solutions.txt");
    // auto words = read_5_letter_words("wordle_solutions.txt");
    // auto words = read_5_letter_words("corncob_lowercase.txt");
    // auto words = read_5_letter_words("dwyl_alpha_words.txt");
    // auto words = read_5_letter_words("sample_100_words.txt");
    // auto words = read_5_letter_words("sample_1000_words.txt");
    // auto&& solutions = words;

    std::cout << words.size() << " words" << std::endl;
    std::cout << solutions.size() << " solutions" << std::endl;

    //test_type best_test{words.front(), words.front(), words.front()};
    test_type best_test{"homed", "clang", "sprit"};
    // test_type best_test{words.front(), words.front(), words.front(), words.front()};
    // test_type best_test{words.front()};
    // test_type best_test{words.front(), words.front()};
    // blast,viper,downy - solutions only
    // homed,clang,sprit - wordle words and solutions
    score_type best_score = calc_score_for_test(solutions, best_test);
    std::cout << "best=" << best_test << "=>" << best_score << std::endl;

    int test_cnt = 0;
    auto start_time = std::chrono::system_clock::now();
    auto last_report_time = start_time;

    auto test_rate = [&]()
    {
        auto now = std::chrono::system_clock::now();
        auto dt = now - start_time;
        return (double)test_cnt * 1e9 / std::chrono::nanoseconds(dt).count();
    };

    while (true)
    {
        test_cnt++;
        test_type test = random_mutate_test(words, best_test);
        test = random_mutate_test(words, test);
        // test_type test = cycle_mutate_test(words, best_test);
        score_type score = calc_score_for_test(solutions, test);
        if (best_score < score)
        {
            best_score = score;
            best_test = test;
            std::cout << test_rate() << " per second [" << test_cnt << "] best=" << best_test << "=>" << best_score << std::endl;
        }

        auto now = std::chrono::system_clock::now();
        auto since_last_report = now - last_report_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(since_last_report).count() >= 5000)
        {
            std::cout << test_rate() << " per second [" << test_cnt << "] best=" << best_test << "=>" << best_score << std::endl;
            last_report_time = now;
        }
    }

    /*
    why do old way and new way not produce same results?

    //old way, worst case for aesir (worst solution is block)
    //block,blond,blood,bloom,blown,bluff,blunt,bobby,bongo,booby,booth,booty,boozy,botch,bough,bound,buddy,buggy,bulky,bully,bunch,bunny,butch,buxom,chock,chuck,chump,chunk,clock,cloth,cloud,clout,clown,cluck,clump,clung,colon,comfy,conch,condo,couch,cough,could,count,coyly,dodgy,dolly,donut,doubt,dough,dowdy,downy,duchy,dully,dummy,dumpy,dutch,flock,flood,flout,flown,fluff,flung,flunk,foggy,folly,found,fully,funky,funny,fuzzy,ghoul,gloom,glyph,godly,golly,goody,goofy,gulch,gully,gumbo,gummy,guppy,hobby,holly,hotly,hound,howdy,humph,hunch,hunky,hutch,jolly,jumbo,jumpy,junto,knock,knoll,known,lobby,lofty,loopy,lowly,lucky,lumpy,lunch,lymph,lynch,mogul,moldy,month,moody,motto,moult,mound,mount,mouth,mucky,muddy,mulch,mummy,munch,nobly,notch,nutty,nylon,nymph,oddly,ought,outdo,outgo,phony,photo,pluck,plumb,plump,plunk,polyp,pooch,poppy,pouch,pound,pouty,pudgy,puffy,pulpy,punch,puppy,putty,pygmy,quoth,thong,thumb,thump,toddy,tooth,touch,tough,uncut,vouch,whoop,woody,wooly,woozy,would,wound,young,youth,aesir, -168, aesir, -168

    //new way, worst case for aesir (worst solution is block)
    //block,blond,blood,bloom,blown,bluff,blunt,bobby,bongo,booby,booth,booty,boozy,botch,bough,bound,buddy,buggy,bulky,bully,bunch,bunny,butch,buxom,chock,chuck,chump,chunk,clock,cloth,cloud,clout,clown,cluck,clump,clung,colon,comfy,conch,condo,couch,cough,could,count,coyly,dodgy,dolly,donut,doubt,dough,dowdy,downy,duchy,dully,dummy,dumpy,dutch,flock,flood,flout,flown,fluff,flung,flunk,foggy,folly,found,fully,funky,funny,fuzzy,ghoul,gloom,glyph,godly,golly,goody,goofy,gulch,gully,gumbo,gummy,guppy,hobby,holly,hotly,hound,howdy,humph,hunch,hunky,hutch,jolly,jumbo,jumpy,junto,knock,knoll,known,lobby,lofty,loopy,lowly,lucky,lumpy,lunch,lymph,lynch,mogul,moldy,month,moody,motto,moult,mound,mount,mouth,mucky,muddy,mulch,mummy,munch,nobly,notch,nutty,nylon,nymph,oddly,ought,outdo,outgo,phony,photo,pluck,plumb,plump,plunk,polyp,pooch,poppy,pouch,pound,pouty,pudgy,puffy,pulpy,punch,puppy,putty,pygmy,quoth,thong,thumb,thump,toddy,tooth,touch,tough,uncut,vouch,whoop,woody,wooly,woozy,would,wound,young,youth

    new way is counting wrong somehow, gets 199 instead of 168

    AHA!!!  new way is incorrect - for a guess word, not all solutions with the same
    color pattern are the same

    AHA AGAIN - the match result was inverted.  old way is agnostic to A vs B or B vs A
    in the match result calc.  new way is sensitive.
    */

    // brute force search
    // for (auto &&w1 : words)
    // {
    //     for (auto &&w2 : words)
    //     {
    //         std::cout << w1 << "," << w2 << std::endl;
    //         for (auto &&w3 : words)
    //         {
    //             test_cnt++;
    //             test_type test{w1, w2, w3};
    //             score_type score = calc_score_for_test(words, test);
    //             if (best_score < score)
    //             {
    //                 best_score = score;
    //                 best_test = test;
    //                 std::cout << "[" << test_cnt << "] best=" << best_test << "=>" << best_score << std::endl;
    //             }
    //         }
    //     }
    // }
}
