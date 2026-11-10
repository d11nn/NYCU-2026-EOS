#include "gallery.hpp"
#include "face_embedder.hpp"

#include <opencv2/core/core.hpp>
#include <iostream>

void Gallery::add ( const std::string &label, const std::vector<float> &embedding )
{
    for ( size_t i = 0; i < persons.size ( ); i++ )
    {
        if ( persons[i].label == label )
        {
            persons[i].samples.push_back ( embedding );
            return;
        }
    }
    Person p;
    p.label = label;
    p.samples.push_back ( embedding );
    persons.push_back ( p );
}

void Gallery::finalize ( )
{
    for ( size_t i = 0; i < persons.size ( ); i++ )
    {
        Person &p = persons[i];
        if ( p.samples.empty ( ) ) continue;
        p.mean.assign ( p.samples[0].size ( ), 0.0f );
        for ( size_t s = 0; s < p.samples.size ( ); s++ )
            for ( size_t d = 0; d < p.mean.size ( ); d++ )
                p.mean[d] += p.samples[s][d];
        l2_normalize ( p.mean );
    }
}

bool Gallery::save ( const std::string &path ) const
{
    cv::FileStorage fs ( path, cv::FileStorage::WRITE );
    if ( !fs.isOpened ( ) ) return false;
    fs << "persons" << "[";
    for ( size_t i = 0; i < persons.size ( ); i++ )
    {
        const Person &p = persons[i];
        // 每列一張照片的 embedding
        cv::Mat samples ( (int) p.samples.size ( ), (int) p.mean.size ( ), CV_32F );
        for ( int r = 0; r < samples.rows; r++ )
            for ( int c = 0; c < samples.cols; c++ )
                samples.at<float> ( r, c ) = p.samples[r][c];
        fs << "{" << "label" << p.label << "samples" << samples << "}";
    }
    fs << "]";
    return true;
}

bool Gallery::load ( const std::string &path )
{
    cv::FileStorage fs ( path, cv::FileStorage::READ );
    if ( !fs.isOpened ( ) )
    {
        std::cerr << "Could not open gallery " << path << std::endl;
        return false;
    }
    persons.clear ( );
    cv::FileNode list = fs["persons"];
    for ( cv::FileNodeIterator it = list.begin ( ); it != list.end ( ); ++it )
    {
        std::string label;
        cv::Mat samples;
        ( *it )["label"] >> label;
        ( *it )["samples"] >> samples;
        for ( int r = 0; r < samples.rows; r++ )
            add ( label, std::vector<float> ( samples.ptr<float> ( r ), samples.ptr<float> ( r ) + samples.cols ) );
    }
    finalize ( );
    return !persons.empty ( );
}

MatchResult Gallery::match ( const std::vector<float> &embedding, float threshold, float margin ) const
{
    MatchResult result;
    int best_index = -1;
    for ( size_t i = 0; i < persons.size ( ); i++ )
    {
        float s = cosine_similarity ( embedding, persons[i].mean );
        if ( s > result.best )
        {
            result.second = result.best;
            result.best = s;
            best_index = (int) i;
        }
        else if ( s > result.second )
        {
            result.second = s;
        }
    }
    if ( best_index >= 0 && result.best >= threshold && result.best - result.second >= margin )
        result.index = best_index;
    return result;
}
