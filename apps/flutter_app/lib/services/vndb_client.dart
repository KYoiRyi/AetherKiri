import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;

import '../models/game_metadata_candidate.dart';

/// Client for VNDB Kana API (visual novel search).
/// https://api.vndb.org/kana/
class VndbClient {
  static const String _baseUrl = 'https://api.vndb.org/kana';
  static const String _vnEndpoint = 'vn';
  static const int _resultsLimit = 20;

  final http.Client _client = http.Client();

  /// Search visual novels by keyword. Returns empty list on network/API error.
  Future<List<GameMetadataCandidate>> search(String keyword) async {
    if (keyword.trim().isEmpty) return [];

    final uri = Uri.parse('$_baseUrl/$_vnEndpoint');
    final body = jsonEncode({
      'filters': ['search', '=', keyword.trim()],
      'fields': 'id,title,alttitle,image{url,thumbnail},developers{name}',
      'results': _resultsLimit,
    });

    try {
      final response = await _client
          .post(uri, body: body, headers: {'Content-Type': 'application/json'})
          .timeout(const Duration(seconds: 15));

      if (response.statusCode != 200) {
        debugPrint('VNDB search failed: HTTP ${response.statusCode}');
        return [];
      }

      final data = jsonDecode(response.body) as Map<String, dynamic>?;
      if (data == null) return [];

      final results = data['results'] as List<dynamic>?;
      if (results == null || results.isEmpty) return [];

      final list = <GameMetadataCandidate>[];
      for (final item in results) {
        if (item is! Map<String, dynamic>) continue;
        final id = item['id']?.toString();
        String title = item['title'] is String
            ? (item['title'] as String).trim()
            : '';
        if (title.isEmpty && item['alttitle'] is String) {
          title = (item['alttitle'] as String).trim();
        }
        if (title.isEmpty) continue;

        String coverUrl = '';
        String? thumbnailUrl;
        final image = item['image'];
        if (image is Map<String, dynamic>) {
          if (image['url'] is String) {
            coverUrl = (image['url'] as String).trim();
          }
          if (image['thumbnail'] is String) {
            final t = (image['thumbnail'] as String).trim();
            if (t.isNotEmpty) thumbnailUrl = t;
          }
        }

        String? developer;
        final developers = item['developers'];
        if (developers is List && developers.isNotEmpty) {
          final first = developers.first;
          if (first is Map<String, dynamic> && first['name'] is String) {
            final name = (first['name'] as String).trim();
            if (name.isNotEmpty) developer = name;
          }
        }

        list.add(
          GameMetadataCandidate(
            title: title,
            coverImageUrl: coverUrl,
            thumbnailUrl: thumbnailUrl,
            developer: developer,
            sourceId: id,
            sourceLabel: 'VNDB',
          ),
        );
      }
      return list;
    } catch (error, stackTrace) {
      debugPrint('VNDB search failed: $error');
      debugPrintStack(stackTrace: stackTrace);
      return [];
    }
  }
}
