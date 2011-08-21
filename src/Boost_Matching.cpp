#include "Boost_Matching.h"

#include <iostream>

namespace OpencvSfM{

  using cv::Ptr;
  using cv::Mat;
  using std::vector;

  vector< cv::Ptr< PointsToTrack > >::iterator MatchingThread::end_matches_it;
  vector< cv::Mat > MatchingThread::masks;
  unsigned int MatchingThread::mininum_points_matches=50;
  PointsMatcher* MatchingThread::match_algorithm = NULL;

  DECLARE_MUTEX( MatchingThread::thread_concurr );
  DECLARE_MUTEX( MatchingThread::thread_unicity );


  MatchingThread::MatchingThread(cv::Ptr<  SequenceAnalyzer> seq_analyser,
    unsigned int i, std::vector< cv::Ptr< PointsToTrack > >::iterator matches_it)
  {
    this->i = i;
    this->matches_it = matches_it;
    this->seq_analyser = seq_analyser;
    this->seq_analyser.addref();//avoid ptr deletion...
  }

  void MatchingThread::operator()()
  {
    Ptr<PointsToTrack> points_to_track_i=( *matches_it );

    points_to_track_i->computeKeypointsAndDesc( false );

    P_MUTEX( thread_unicity );
    Ptr<PointsMatcher> point_matcher = match_algorithm->clone( true );
    point_matcher->add( points_to_track_i );
    V_MUTEX( thread_unicity );
    point_matcher->train( );

    vector< Ptr< PointsToTrack > >::iterator matches_it1 = matches_it+1;
    unsigned int j=i+1;
    while ( matches_it1 != end_matches_it )
    {
      Ptr<PointsToTrack> points_to_track_j=( *matches_it1 );

      points_to_track_j->computeKeypointsAndDesc( false );

      P_MUTEX( thread_unicity );
      Ptr<PointsMatcher> point_matcher1 = match_algorithm->clone( true );
      point_matcher1->add( points_to_track_j );
      V_MUTEX( thread_unicity );
      point_matcher1->train( );

      vector< cv::DMatch > matches_i_j;
      point_matcher->crossMatch( point_matcher1, matches_i_j, masks );

      //First compute points matches:
      unsigned int size_match=matches_i_j.size( );
      vector<cv::Point2f> srcP;
      vector<cv::Point2f> destP;
      vector<uchar> status;

      //vector<KeyPoint> points1 = point_matcher->;
      for( size_t cpt = 0; cpt < size_match; ++cpt ){
        const cv::KeyPoint &key1 = point_matcher1->getKeypoint(
          matches_i_j[ cpt ].queryIdx );
        const cv::KeyPoint &key2 = point_matcher->getKeypoint(
          matches_i_j[ cpt ].trainIdx );
        srcP.push_back( cv::Point2f( key1.pt.x,key1.pt.y ) );
        destP.push_back( cv::Point2f( key2.pt.x,key2.pt.y ) );
        status.push_back( 1 );
      }

      //free some memory:
      point_matcher1->clear();
      points_to_track_j->free_descriptors();

      Mat fundam = cv::findFundamentalMat( srcP, destP, status, cv::FM_RANSAC, 1 );

      unsigned int nbErrors = 0, nb_iter=0;
      //refine the mathing :
      size_match = status.size( );
      for( size_t cpt = 0; cpt < size_match; ++cpt ){
        if( status[ cpt ] == 0 )
        {
          size_match--;
          status[ cpt ] = status[ size_match ];
          status.pop_back( );
          srcP[ cpt ] = srcP[ size_match ];
          srcP.pop_back( );
          destP[ cpt ] = destP[ size_match ];
          destP.pop_back( );
          matches_i_j[ cpt ] = matches_i_j[ size_match ];
          matches_i_j.pop_back( );
          cpt--;
          ++nbErrors;
        }
      }

      while( nbErrors > size_match/10 && nb_iter < 4 &&
        matches_i_j.size( ) > mininum_points_matches )
      {
        fundam = cv::findFundamentalMat( srcP, destP, status, cv::FM_RANSAC, 1.5 );

        //refine the mathing :
        nbErrors =0 ;
        size_match = status.size( );
        for( size_t cpt = 0; cpt < size_match; ++cpt ){
          if( status[ cpt ] == 0 )
          {
            size_match--;
            status[ cpt ] = status[ size_match ];
            status.pop_back( );
            srcP[ cpt ] = srcP[ size_match ];
            srcP.pop_back( );
            destP[ cpt ] = destP[ size_match ];
            destP.pop_back( );
            matches_i_j[ cpt ] = matches_i_j[ size_match ];
            matches_i_j.pop_back( );
            cpt--;
            ++nbErrors;
          }
        }
        nb_iter++;
      };

      //refine the mathing:
      fundam = cv::findFundamentalMat( srcP, destP, status, cv::FM_LMEDS );

      //refine the mathing :
      size_match = status.size( );
      for( size_t cpt = 0; cpt < size_match; ++cpt ){
        if( status[ cpt ] == 0 )
        {
          size_match--;
          status[ cpt ] = status[ size_match ];
          status.pop_back( );
          srcP[ cpt ] = srcP[ size_match ];
          srcP.pop_back( );
          destP[ cpt ] = destP[ size_match ];
          destP.pop_back( );
          matches_i_j[ cpt ] = matches_i_j[ size_match ];
          matches_i_j.pop_back( );
          cpt--;
          ++nbErrors;
        }
      }

      if( matches_i_j.size( ) > mininum_points_matches && nb_iter < 4 )
      {
        P_MUTEX( thread_unicity );
        seq_analyser->addMatches( matches_i_j,i,j );
        std::clog<<"find "<<matches_i_j.size( )<<
          " matches between "<<i<<" "<<j<<std::endl;
        V_MUTEX( thread_unicity );
      }else
      {
        std::clog<<"can't find matches between "<<i<<" "<<j<<std::endl;
      }
      j++;
      matches_it1++;
    }

    P_MUTEX( thread_unicity );
    point_matcher->clear();
    points_to_track_i->free_descriptors();//save memory...
    V_MUTEX( thread_unicity );
    V_MUTEX( MatchingThread::thread_concurr );//wake up waiting thread
  };
}