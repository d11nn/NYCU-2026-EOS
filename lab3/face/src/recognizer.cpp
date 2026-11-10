#include "recognizer.hpp"
#include "tracker.hpp"

#include <chrono>

bool Recognizer::init ( const Config &cfg, const Gallery *g )
{
    if ( !aligner.load ( cfg.eye_cascade ) ) return false;
    if ( !embedder.load ( cfg ) ) return false;
    gallery = g;
    threshold = (float) cfg.sim_threshold;
    margin = (float) cfg.sim_margin;
    return true;
}

void Recognizer::start ( )
{
    worker = std::thread ( &Recognizer::loop, this );
}

void Recognizer::finish ( )
{
    {
        std::lock_guard<std::mutex> lock ( mtx );
        stopping = true;
    }
    cond.notify_one ( );
    if ( worker.joinable ( ) ) worker.join ( );
}

bool Recognizer::push ( const RecogJob &job )
{
    {
        std::lock_guard<std::mutex> lock ( mtx );
        if ( jobs.size ( ) >= MAX_JOBS ) return false;
        jobs.push ( job );
    }
    cond.notify_one ( );
    return true;
}

std::vector<RecogResult> Recognizer::take_results ( )
{
    std::lock_guard<std::mutex> lock ( mtx );
    std::vector<RecogResult> out;
    out.swap ( results );
    return out;
}

void Recognizer::loop ( )
{
    for ( ; ; )
    {
        RecogJob job;
        {
            std::unique_lock<std::mutex> lock ( mtx );
            cond.wait ( lock, [this] { return stopping || !jobs.empty ( ); } );
            if ( stopping ) return;
            job = jobs.front ( );
            jobs.pop ( );
        }

        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now ( );
        cv::Mat aligned = aligner.align ( job.region, job.face );
        std::vector<float> feature = embedder.embed ( aligned );
        MatchResult m = gallery->match ( feature, threshold, margin );
        double ms = std::chrono::duration<double, std::milli> ( std::chrono::steady_clock::now ( ) - t0 ).count ( );

        RecogResult r;
        r.track_id = job.track_id;
        r.label = m.index >= 0 ? m.index : LABEL_UNKNOWN;
        r.score = m.best;
        r.millis = ms;
        {
            std::lock_guard<std::mutex> lock ( mtx );
            results.push_back ( r );
        }
    }
}
