/**
 * Tests ProfileStorage object with creditCards records.
 */

"use strict";

const {ProfileStorage} = Cu.import("resource://formautofill/ProfileStorage.jsm", {});

const TEST_STORE_FILE_NAME = "test-credit-card.json";

const TEST_CREDIT_CARD_1 = {
  "cc-name": "John Doe",
  "cc-number": "1234567812345678",
  "cc-exp-month": 4,
  "cc-exp-year": 2017,
};

const TEST_CREDIT_CARD_2 = {
  "cc-name": "Timothy Berners-Lee",
  "cc-number": "1111222233334444",
  "cc-exp-month": 12,
  "cc-exp-year": 2022,
};

const TEST_CREDIT_CARD_3 = {
  "cc-number": "9999888877776666",
  "cc-exp-month": 1,
  "cc-exp-year": 2000,
};

const TEST_CREDIT_CARD_4 = {
  "cc-name": "Foo Bar",
  "cc-number": "9999888877776666",
};

const TEST_CREDIT_CARD_WITH_EMPTY_FIELD = {
  "cc-name": "",
  "cc-number": "1234123412341234",
  "cc-exp-month": 1,
};

const TEST_CREDIT_CARD_WITH_2_DIGITS_YEAR = {
  "cc-number": "1234123412341234",
  "cc-exp-month": 1,
  "cc-exp-year": 12,
};

const TEST_CREDIT_CARD_WITH_INVALID_FIELD = {
  "cc-name": "John Doe",
  "cc-number": "1234123412341234",
  invalidField: "INVALID",
};

const TEST_CREDIT_CARD_WITH_INVALID_EXPIRY_DATE = {
  "cc-name": "John Doe",
  "cc-number": "1111222233334444",
  "cc-exp-month": 13,
  "cc-exp-year": -3,
};

const TEST_CREDIT_CARD_WITH_SPACES_BETWEEN_DIGITS = {
  "cc-name": "John Doe",
  "cc-number": "1111 2222 3333 4444",
};

const MERGE_TESTCASES = [
  {
    description: "Merge a superset",
    creditCardInStorage: {
      "cc-number": "1234567812345678",
      "cc-exp-month": 4,
      "cc-exp-year": 2017,
    },
    creditCardToMerge: {
      "cc-name": "John Doe",
      "cc-number": "1234567812345678",
      "cc-exp-month": 4,
      "cc-exp-year": 2017,
    },
    expectedCreditCard: {
      "cc-name": "John Doe",
      "cc-number": "1234567812345678",
      "cc-exp-month": 4,
      "cc-exp-year": 2017,
    },
  },
  {
    description: "Merge a subset",
    creditCardInStorage: {
      "cc-name": "John Doe",
      "cc-number": "1234567812345678",
      "cc-exp-month": 4,
      "cc-exp-year": 2017,
    },
    creditCardToMerge: {
      "cc-number": "1234567812345678",
      "cc-exp-month": 4,
      "cc-exp-year": 2017,
    },
    expectedCreditCard: {
      "cc-name": "John Doe",
      "cc-number": "1234567812345678",
      "cc-exp-month": 4,
      "cc-exp-year": 2017,
    },
    noNeedToUpdate: true,
  },
  {
    description: "Merge an creditCard with partial overlaps",
    creditCardInStorage: {
      "cc-name": "John Doe",
      "cc-number": "1234567812345678",
    },
    creditCardToMerge: {
      "cc-number": "1234567812345678",
      "cc-exp-month": 4,
      "cc-exp-year": 2017,
    },
    expectedCreditCard: {
      "cc-name": "John Doe",
      "cc-number": "1234567812345678",
      "cc-exp-month": 4,
      "cc-exp-year": 2017,
    },
  },
];

let prepareTestCreditCards = async function(path) {
  let profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  let onChanged = TestUtils.topicObserved("formautofill-storage-changed",
                                          (subject, data) => data == "add");
  do_check_true(profileStorage.creditCards.add(TEST_CREDIT_CARD_1));
  await onChanged;
  do_check_true(profileStorage.creditCards.add(TEST_CREDIT_CARD_2));
  await onChanged;
  await profileStorage._saveImmediately();
};

let reCCNumber = /^(\*+)(.{4})$/;

let do_check_credit_card_matches = (creditCardWithMeta, creditCard) => {
  for (let key in creditCard) {
    if (key == "cc-number") {
      let matches = reCCNumber.exec(creditCardWithMeta["cc-number"]);
      do_check_neq(matches, null);
      do_check_eq(creditCardWithMeta["cc-number"].length, creditCard["cc-number"].length);
      do_check_eq(creditCard["cc-number"].endsWith(matches[2]), true);
      do_check_neq(creditCard["cc-number-encrypted"], "");
    } else {
      do_check_eq(creditCardWithMeta[key], creditCard[key]);
    }
  }
};

add_task(async function test_initialize() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  let profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  do_check_eq(profileStorage._store.data.version, 1);
  do_check_eq(profileStorage._store.data.creditCards.length, 0);

  let data = profileStorage._store.data;
  Assert.deepEqual(data.creditCards, []);

  await profileStorage._saveImmediately();

  profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  Assert.deepEqual(profileStorage._store.data, data);
});

add_task(async function test_getAll() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  let creditCards = profileStorage.creditCards.getAll();

  do_check_eq(creditCards.length, 2);
  do_check_credit_card_matches(creditCards[0], TEST_CREDIT_CARD_1);
  do_check_credit_card_matches(creditCards[1], TEST_CREDIT_CARD_2);

  // Check computed fields.
  do_check_eq(creditCards[0]["cc-given-name"], "John");
  do_check_eq(creditCards[0]["cc-family-name"], "Doe");
  do_check_eq(creditCards[0]["cc-exp"], "2017-04");

  // Test with rawData set.
  creditCards = profileStorage.creditCards.getAll({rawData: true});
  do_check_eq(creditCards[0]["cc-given-name"], undefined);
  do_check_eq(creditCards[0]["cc-family-name"], undefined);
  do_check_eq(creditCards[0]["cc-exp"], undefined);

  // Modifying output shouldn't affect the storage.
  creditCards[0]["cc-name"] = "test";
  do_check_credit_card_matches(profileStorage.creditCards.getAll()[0], TEST_CREDIT_CARD_1);
});

add_task(async function test_get() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  let creditCards = profileStorage.creditCards.getAll();
  let guid = creditCards[0].guid;

  let creditCard = profileStorage.creditCards.get(guid);
  do_check_credit_card_matches(creditCard, TEST_CREDIT_CARD_1);

  // Modifying output shouldn't affect the storage.
  creditCards[0]["cc-name"] = "test";
  do_check_credit_card_matches(profileStorage.creditCards.get(guid), TEST_CREDIT_CARD_1);

  do_check_eq(profileStorage.creditCards.get("INVALID_GUID"), null);
});

add_task(async function test_add() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  let creditCards = profileStorage.creditCards.getAll();

  do_check_eq(creditCards.length, 2);

  do_check_credit_card_matches(creditCards[0], TEST_CREDIT_CARD_1);
  do_check_credit_card_matches(creditCards[1], TEST_CREDIT_CARD_2);

  do_check_neq(creditCards[0].guid, undefined);
  do_check_eq(creditCards[0].version, 1);
  do_check_neq(creditCards[0].timeCreated, undefined);
  do_check_eq(creditCards[0].timeLastModified, creditCards[0].timeCreated);
  do_check_eq(creditCards[0].timeLastUsed, 0);
  do_check_eq(creditCards[0].timesUsed, 0);

  // Empty string should be deleted before saving.
  profileStorage.creditCards.add(TEST_CREDIT_CARD_WITH_EMPTY_FIELD);
  let creditCard = profileStorage.creditCards.data[2];
  do_check_eq(creditCard["cc-exp-month"], TEST_CREDIT_CARD_WITH_EMPTY_FIELD["cc-exp-month"]);
  do_check_eq(creditCard["cc-name"], undefined);

  Assert.throws(() => profileStorage.creditCards.add(TEST_CREDIT_CARD_WITH_INVALID_FIELD),
    /"invalidField" is not a valid field\./);
});

add_task(async function test_update() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  let creditCards = profileStorage.creditCards.getAll();
  let guid = creditCards[1].guid;
  let timeLastModified = creditCards[1].timeLastModified;

  let onChanged = TestUtils.topicObserved("formautofill-storage-changed",
                                          (subject, data) => data == "update");

  do_check_neq(creditCards[1]["cc-name"], undefined);
  profileStorage.creditCards.update(guid, TEST_CREDIT_CARD_3);
  await onChanged;
  await profileStorage._saveImmediately();

  profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  let creditCard = profileStorage.creditCards.get(guid);

  do_check_eq(creditCard["cc-name"], undefined);
  do_check_neq(creditCard.timeLastModified, timeLastModified);
  do_check_credit_card_matches(creditCard, TEST_CREDIT_CARD_3);

  // Empty string should be deleted while updating.
  profileStorage.creditCards.update(profileStorage.creditCards.data[0].guid, TEST_CREDIT_CARD_WITH_EMPTY_FIELD);
  creditCard = profileStorage.creditCards.data[0];
  do_check_eq(creditCard["cc-exp-month"], TEST_CREDIT_CARD_WITH_EMPTY_FIELD["cc-exp-month"]);
  do_check_eq(creditCard["cc-name"], undefined);

  Assert.throws(
    () => profileStorage.creditCards.update("INVALID_GUID", TEST_CREDIT_CARD_3),
    /No matching record\./
  );

  Assert.throws(
    () => profileStorage.creditCards.update(guid, TEST_CREDIT_CARD_WITH_INVALID_FIELD),
    /"invalidField" is not a valid field\./
  );
});

add_task(async function test_validate() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;

  let profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  profileStorage.creditCards.add(TEST_CREDIT_CARD_WITH_INVALID_EXPIRY_DATE);
  profileStorage.creditCards.add(TEST_CREDIT_CARD_WITH_2_DIGITS_YEAR);
  profileStorage.creditCards.add(TEST_CREDIT_CARD_WITH_SPACES_BETWEEN_DIGITS);

  let creditCards = profileStorage.creditCards.getAll();

  do_check_eq(creditCards[0]["cc-exp-month"], undefined);
  do_check_eq(creditCards[0]["cc-exp-year"], undefined);
  do_check_eq(creditCards[0]["cc-exp"], undefined);

  let month = TEST_CREDIT_CARD_WITH_2_DIGITS_YEAR["cc-exp-month"];
  let year = parseInt(TEST_CREDIT_CARD_WITH_2_DIGITS_YEAR["cc-exp-year"], 10) + 2000;
  do_check_eq(creditCards[1]["cc-exp-month"], month);
  do_check_eq(creditCards[1]["cc-exp-year"], year);
  do_check_eq(creditCards[1]["cc-exp"], year + "-" + month.toString().padStart(2, "0"));

  do_check_eq(creditCards[2]["cc-number"].length, 16);
});

add_task(async function test_notifyUsed() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  let creditCards = profileStorage.creditCards.getAll();
  let guid = creditCards[1].guid;
  let timeLastUsed = creditCards[1].timeLastUsed;
  let timesUsed = creditCards[1].timesUsed;

  let onChanged = TestUtils.topicObserved("formautofill-storage-changed",
                                          (subject, data) => data == "notifyUsed");

  profileStorage.creditCards.notifyUsed(guid);
  await onChanged;
  await profileStorage._saveImmediately();

  profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  let creditCard = profileStorage.creditCards.get(guid);

  do_check_eq(creditCard.timesUsed, timesUsed + 1);
  do_check_neq(creditCard.timeLastUsed, timeLastUsed);

  Assert.throws(() => profileStorage.creditCards.notifyUsed("INVALID_GUID"),
    /No matching record\./);
});

add_task(async function test_remove() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  let creditCards = profileStorage.creditCards.getAll();
  let guid = creditCards[1].guid;

  let onChanged = TestUtils.topicObserved("formautofill-storage-changed",
                                          (subject, data) => data == "remove");

  do_check_eq(creditCards.length, 2);

  profileStorage.creditCards.remove(guid);
  await onChanged;
  await profileStorage._saveImmediately();

  profileStorage = new ProfileStorage(path);
  await profileStorage.initialize();

  creditCards = profileStorage.creditCards.getAll();

  do_check_eq(creditCards.length, 1);

  do_check_eq(profileStorage.creditCards.get(guid), null);
});

MERGE_TESTCASES.forEach((testcase) => {
  add_task(async function test_merge() {
    do_print("Starting testcase: " + testcase.description);
    let profileStorage = await initProfileStorage(TEST_STORE_FILE_NAME,
                                                  [testcase.creditCardInStorage],
                                                  "creditCards");
    let creditCards = profileStorage.creditCards.getAll();
    let guid = creditCards[0].guid;
    let timeLastModified = creditCards[0].timeLastModified;
    // Merge creditCard and verify the guid in notifyObservers subject
    let onMerged = TestUtils.topicObserved(
      "formautofill-storage-changed",
      (subject, data) =>
        data == "update" && subject.QueryInterface(Ci.nsISupportsString).data == guid
    );
    // Force to create sync metadata.
    profileStorage.creditCards.pullSyncChanges();
    do_check_eq(getSyncChangeCounter(profileStorage.creditCards, guid), 1);
    Assert.ok(profileStorage.creditCards.mergeIfPossible(guid, testcase.creditCardToMerge));
    if (!testcase.noNeedToUpdate) {
      await onMerged;
    }
    creditCards = profileStorage.creditCards.getAll();
    Assert.equal(creditCards.length, 1);
    do_check_credit_card_matches(creditCards[0], testcase.expectedCreditCard);
    if (!testcase.noNeedToUpdate) {
      // Record merging should update timeLastModified and bump the change counter.
      Assert.notEqual(creditCards[0].timeLastModified, timeLastModified);
      do_check_eq(getSyncChangeCounter(profileStorage.creditCards, guid), 2);
    } else {
      // Subset record merging should not update timeLastModified and the change
      // counter is still the same.
      Assert.equal(creditCards[0].timeLastModified, timeLastModified);
      do_check_eq(getSyncChangeCounter(profileStorage.creditCards, guid), 1);
    }
  });
});

add_task(async function test_merge_unable_merge() {
  let profileStorage = await initProfileStorage(TEST_STORE_FILE_NAME,
                                                [TEST_CREDIT_CARD_1],
                                                "creditCards");

  let creditCards = profileStorage.creditCards.getAll();
  let guid = creditCards[0].guid;
  // Force to create sync metadata.
  profileStorage.creditCards.pullSyncChanges();
  do_check_eq(getSyncChangeCounter(profileStorage.creditCards, guid), 1);

  // Unable to merge because of conflict
  let anotherCreditCard = profileStorage.creditCards._clone(TEST_CREDIT_CARD_1);
  anotherCreditCard["cc-name"] = "Foo Bar";
  do_check_eq(profileStorage.creditCards.mergeIfPossible(guid, anotherCreditCard), false);
  // The change counter is unchanged.
  do_check_eq(getSyncChangeCounter(profileStorage.creditCards, guid), 1);

  // Unable to merge because no credit card number
  anotherCreditCard = profileStorage.creditCards._clone(TEST_CREDIT_CARD_1);
  anotherCreditCard["cc-number"] = "";
  do_check_eq(profileStorage.creditCards.mergeIfPossible(guid, anotherCreditCard), false);
  // The change counter is still unchanged.
  do_check_eq(getSyncChangeCounter(profileStorage.creditCards, guid), 1);
});

add_task(async function test_mergeToStorage() {
  let profileStorage = await initProfileStorage(TEST_STORE_FILE_NAME,
                                                [TEST_CREDIT_CARD_3, TEST_CREDIT_CARD_4],
                                                "creditCards");
  // Merge a creditCard to storage
  let anotherCreditCard = profileStorage.creditCards._clone(TEST_CREDIT_CARD_3);
  anotherCreditCard["cc-name"] = "Foo Bar";
  do_check_eq(profileStorage.creditCards.mergeToStorage(anotherCreditCard).length, 2);
  do_check_eq(profileStorage.creditCards.getAll()[0]["cc-name"], "Foo Bar");
  do_check_eq(profileStorage.creditCards.getAll()[0]["cc-exp"], "2000-01");
  do_check_eq(profileStorage.creditCards.getAll()[1]["cc-name"], "Foo Bar");
  do_check_eq(profileStorage.creditCards.getAll()[1]["cc-exp"], "2000-01");
});
