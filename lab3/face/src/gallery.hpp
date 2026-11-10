#pragma once

#include <string>
#include <vector>

// 登錄資料：每位組員一組 embedding，比對時用它們的平均（再正規化）當範本
struct Person
{
    std::string label;                          // 顯示的文字，例如學號
    std::vector<std::vector<float> > samples;   // 每張登錄照片的 embedding
    std::vector<float> mean;                    // 平均後再 L2 正規化
};

struct MatchResult
{
    int index = -1;         // 最像的人在 persons 裡的位置；-1 代表 unknown
    float best = -1.0f;     // 最高的相似度
    float second = -1.0f;   // 第二高的相似度
};

class Gallery
{
public:
    void add ( const std::string &label, const std::vector<float> &embedding );
    void finalize ( );      // 重新計算每個人的平均範本

    bool save ( const std::string &path ) const;
    bool load ( const std::string &path );

    // 判定規則：best >= threshold 且 best - second >= margin 才算認出，否則 unknown
    MatchResult match ( const std::vector<float> &embedding, float threshold, float margin ) const;

    const std::vector<Person> &people ( ) const { return persons; }

private:
    std::vector<Person> persons;
};
