/*
 * Copyright (C) 2014 Canonical, Ltd.
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 3 of the GNU Lesser General Public License as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Pete Woods <pete.woods@canonical.com>
 */

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <vimeo/scope/localisation.h>
#include <vimeo/scope/query.h>

#include <unity/scopes/Annotation.h>
#include <unity/scopes/CategorisedResult.h>
#include <unity/scopes/CategoryRenderer.h>
#include <unity/scopes/OnlineAccountClient.h>
#include <unity/scopes/QueryBase.h>
#include <unity/scopes/SearchReply.h>

namespace sc = unity::scopes;
namespace alg = boost::algorithm;

using namespace std;
using namespace vimeo::api;
using namespace vimeo::scope;

namespace {

const static string SEARCH_CATEGORY_TEMPLATE = R"(
{
  "schema-version": 1,
  "template": {
    "category-layout": "grid",
    "card-size": "large",
    "overlay": true
  },
  "components": {
    "title": "title",
    "art" : {
      "field": "art",
      "aspect-ratio": 2.0
    },
    "subtitle": "username"
  }
}
)";

const static string SEARCH_CATEGORY_LOGIN_NAG = R"(
{
  "schema-version": 1,
  "template": {
    "category-layout": "grid",
    "card-size": "large",
    "card-background": "color:///#1ab7ea"
  },
  "components": {
    "title": "title",
    "background": "background",
    "art" : {
      "aspect-ratio": 100.0
    }
  }
}
)";

}

Query::Query(const sc::CannedQuery &query, const sc::SearchMetadata &metadata,
        Config::Ptr config) :
        sc::SearchQueryBase(query, metadata), client_(config) {
}

void Query::cancelled() {
    client_.cancel();
}

void Query::add_login_nag(const sc::SearchReplyProxy &reply) {
    if (getenv("VIMEO_SCOPE_IGNORE_ACCOUNTS")) {
        return;
    }

    sc::CategoryRenderer rdr(SEARCH_CATEGORY_LOGIN_NAG);
    auto cat = reply->register_category("vimeo_login_nag", "", "", rdr);

    sc::CategorisedResult res(cat);
    res.set_title(_("Log-in to Vimeo"));

    sc::OnlineAccountClient oa_client(SCOPE_INSTALL_NAME, "sharing", SCOPE_ACCOUNTS_NAME);
    oa_client.register_account_login_item(res,
                                          query(),
                                          sc::OnlineAccountClient::InvalidateResults,
                                          sc::OnlineAccountClient::DoNothing);

    reply->push(res);
}

void Query::run(sc::SearchReplyProxy const& reply) {
    try {
        const sc::CannedQuery &query(sc::SearchQueryBase::query());

        string query_string = alg::trim_copy(query.query_string());

        Client::VideoList videos;

        if (query_string.empty()) {

            sc::Department::SPtr all_depts = sc::Department::create("", query,
                    "My Feed");
            for (Channel::Ptr channel : client_.channels()) {
                sc::Department::SPtr dept = sc::Department::create(
                        channel->id(), query, channel->name());
                all_depts->add_subdepartment(dept);
            }

            bool include_login_nag = !client_.config()->authenticated;

            if (alg::starts_with(query.department_id(), "aggregated:")) {
                // Need to add a dummy department to pass the validation check
                sc::Department::SPtr dummy = sc::Department::create(
                        query.department_id(), query, " ");
                all_depts->add_subdepartment(dummy);

                include_login_nag = false;
                videos = client_.channels_videos("staffpicks");
            } else if (!query.department_id().empty()) {
                videos = client_.channels_videos(query.department_id());
            } else if (client_.config()->authenticated) {
                videos = client_.feed();
            } else {
                videos = client_.channels_videos("staffpicks");
            }

            reply->register_departments(all_depts);

            if (include_login_nag) {
                add_login_nag(reply);
            }
        } else {
            videos = client_.videos(query_string);
        }

        auto cat = reply->register_category("vimeo", "", "",
                sc::CategoryRenderer(SEARCH_CATEGORY_TEMPLATE));

        for (Video::Ptr video : videos) {
            sc::CategorisedResult res(cat);
            res.set_uri(video->uri());
            res.set_title(video->name());
            res.set_art(video->picture());
            res["description"] = video->description();
            res["username"] = video->username();

            if (!reply->push(res)) {
                return;
            }
        }
    } catch (domain_error &e) {
    }
}

