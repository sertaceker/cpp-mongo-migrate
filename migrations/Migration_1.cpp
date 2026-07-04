#ifndef MIGRATION_1_CPP
#define MIGRATION_1_CPP
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>

#include "../migration.hpp"

DECLARE_MIGRATION(Migration_1, 1)

void Migration_1::up(mongocxx::client& client, mongocxx::client_session& session)
{
    auto collection = client["testDB"]["testCollection1"];
    const auto testIndex = bsoncxx::from_json(R"(
    {
        "testField": 1
    })");
    collection.create_index(session, testIndex.view());

    const auto testDocument1 = bsoncxx::from_json(R"(
    {
        "testField": "value1",
        "otherField": 123
    })");
    collection.insert_one(session, testDocument1.view());
}

void Migration_1::down(mongocxx::client& client, mongocxx::client_session& session)
{
    auto collection = client["testDB"]["testCollection1"];
    collection.drop();
}


#endif //MIGRATION_1_CPP