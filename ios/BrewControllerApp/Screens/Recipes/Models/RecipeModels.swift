// Models for the Recipes screens that Core/Models does not have: the reply of POST /api/recipe and where
// a recipe stands with the recipe book (GET /api/library/pending). Lenient like LenientDecoding.swift.
import Foundation

/// `{"ok":true,"id":N}` from POST /api/recipe.
struct RecipeIdReply: Codable, Sendable, Equatable {
    var ok: Bool = true
    var id: Int = 0

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        ok = c.lenient(.ok, true)
        id = c.lenient(.id, 0)
    }
}

/// One recipe's place in the recipe book: `recipes[]` of /api/library/pending.
struct RecipeShareEntry: Codable, Sendable, Equatable, Identifiable {
    var id: Int = 0
    /// "local", "queued", "sent", "review", "published", "declined" or "refused".
    var state: String = "local"
    var note: String = ""
    var libraryId: String = ""

    init() {}

    enum CodingKeys: String, CodingKey {
        case id, state, note
        case libraryId = "library_id"
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        state = c.lenient(.state, "local")
        note = c.lenient(.note, "")
        libraryId = c.lenient(.libraryId, "")
    }
}

/// What the Recipes screen reads of /api/library/pending: whether the shared library is linked, and each
/// recipe's state in the recipe book.
struct RecipePendingModel: Codable, Sendable, Equatable {
    var linked: Bool = false
    var sharing: Bool = false
    var recipes: [RecipeShareEntry] = []

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        linked = c.lenient(.linked, false)
        sharing = c.lenient(.sharing, false)
        recipes = c.lenient(.recipes, [])
    }

    func entry(for recipeId: Int) -> RecipeShareEntry? { recipes.first { $0.id == recipeId } }
}

/// The words of a recipe's state in the recipe book (WebPage.h SHARE_WORDS).
enum RecipeShareText {
    static func state(_ state: String) -> String {
        switch state {
        case "queued": return "waiting to be sent"
        case "sent": return "sent"
        case "review": return "in review"
        case "published": return "published in the recipe book"
        case "declined": return "declined"
        case "refused": return "refused"
        default: return state
        }
    }
}
